/*
 * Intel 82540EM (e1000) NIC driver for JOS.
 *
 * Architecture overview
 * ---------------------
 * The driver lives in the JOS file-system server process (fs/serv.c),
 * which runs in user space but has elevated privileges:
 *   - IOPL=3 (I/O port access for legacy PCI)
 *   - sys_map_physical_region() to map MMIO without going through the VFS
 *
 * The NIC uses DMA to move packet data between its on-chip FIFO and RAM.
 * Two ring buffers of descriptors mediate this:
 *
 *   TX ring  -- driver fills a descriptor (buffer PA + length + flags)
 *               and advances the tail pointer (TDT); the NIC reads the
 *               descriptor, DMAs the payload from RAM to its FIFO, and
 *               then sets the DD (Descriptor Done) status bit.
 *
 *   RX ring  -- driver pre-fills every descriptor with the PA of a
 *               2-KB receive buffer and gives them to the NIC by writing
 *               RDT.  When a frame arrives the NIC DMA-writes it into the
 *               next buffer, sets DD, and advances the hardware head (RDH).
 *               The driver reclaims descriptors by reading DD and returning
 *               each one to the NIC after copying the data.
 *
 * Both rings are operated in polling mode (no interrupts).  This keeps the
 * implementation simple; Part 2 of the assignment can add interrupt-driven
 * I/O on top of the same descriptor rings without changing the hardware
 * layout.
 *
 * Reference: Intel 82540EM Gigabit Ethernet Controller Software Developer
 * Manual (document 317453).  QEMU emulates this model as "e1000".
 */

#include "e1000.h"
#include <inc/lib.h>
#include <inc/string.h>

/* The single NIC instance managed by this driver */
static struct E1000 nic;

/* -----------------------------------------------------------------------
 * MMIO register helpers
 *
 * All e1000 registers are 32-bit wide and accessed via MMIO.  The read-
 * back after every write flushes the write through the PCI bus (PCI write
 * posting means writes can sit in a buffer; a subsequent read forces them
 * out before we proceed).
 * ----------------------------------------------------------------------- */
static inline uint32_t
e1000_reg_read(uint32_t off) {
    return *(volatile uint32_t *)(nic.mmio + off);
}

static inline void
e1000_reg_write(uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(nic.mmio + off) = val;
    (void)*(volatile uint32_t *)(nic.mmio + off); /* flush write posting */
}

/* -----------------------------------------------------------------------
 * EEPROM read via the EERD register
 *
 * The 82540EM stores the factory MAC address in a small serial EEPROM.
 * To read a 16-bit word at address `addr`:
 *   1. Write EERD with START=1 and the target address.
 *   2. Poll until the DONE bit is set.
 *   3. Extract bits [31:16] — that is the word.
 *
 * MAC word layout: word 0 = MAC[1:0], word 1 = MAC[3:2], word 2 = MAC[5:4].
 * ----------------------------------------------------------------------- */
static uint16_t
e1000_eerd_read(uint8_t addr) {
    e1000_reg_write(E1000_EERD,
        E1000_EERD_START | ((uint32_t)addr << E1000_EERD_ADDR_SHIFT));
    uint32_t v;
    do {
        v = e1000_reg_read(E1000_EERD);
    } while (!(v & E1000_EERD_DONE));
    return (uint16_t)(v >> E1000_EERD_DATA_SHIFT);
}

static void
e1000_mac_read(void) {
    for (int i = 0; i < 3; i++) {
        uint16_t word = e1000_eerd_read((uint8_t)i);
        nic.mac[i * 2]     = (uint8_t)(word & 0xFF);
        nic.mac[i * 2 + 1] = (uint8_t)(word >> 8);
    }
}

/* -----------------------------------------------------------------------
 * Map BAR0 MMIO registers
 *
 * BAR0 holds the 128 KB register space.  We map it non-cacheable (PROT_CD)
 * so that every register read/write hits the device and is not served from
 * a CPU cache line.
 * ----------------------------------------------------------------------- */
static void
e1000_map(void) {
    uintptr_t pa = get_bar_address(nic.pcidev, 0);
    uint32_t  sz = get_bar_size(nic.pcidev, 0);
    if (sz > E1000_MMIO_SIZE)
        sz = E1000_MMIO_SIZE;

    int r = sys_map_physical_region(pa, CURENVID,
                                    (void *)E1000_MMIO_VADDR,
                                    sz, PROT_RW | PROT_CD);
    if (r < 0)
        panic("e1000: cannot map MMIO (pa=0x%lx, err=%d)\n",
              (unsigned long)pa, r);

    nic.mmio = (volatile uint8_t *)E1000_MMIO_VADDR;
}

/* -----------------------------------------------------------------------
 * Allocate DMA memory
 *
 * We use sys_alloc_region() to allocate kernel-backed pages at a fixed
 * virtual address.  get_phys_addr() then reads the user page-table (uvpt)
 * to translate each virtual address to its physical address, which we
 * program into the NIC registers.
 *
 * All pages are touched immediately after allocation to:
 *   - Pin the physical pages (prevent them from being re-mapped).
 *   - Verify the allocation succeeded for every page.
 * The kernel zeroes newly allocated pages, so all descriptor status fields
 * start at 0 (== "not yet used by hardware").
 * ----------------------------------------------------------------------- */
static void
e1000_alloc_dma(void) {
    void *base = (void *)E1000_DMA_VADDR;
    int r = sys_alloc_region(CURENVID, base,
                             E1000_DMA_PAGES * PAGE_SIZE, PROT_RW | PROT_CD);
    if (r < 0)
        panic("e1000: cannot allocate DMA pages (err=%d)\n", r);

    for (int i = 0; i < E1000_DMA_PAGES; i++) {
        volatile char *p = (volatile char *)base + (size_t)i * PAGE_SIZE;
        *p = 0; /* touch page to ensure physical backing */
    }

    uint8_t *b  = (uint8_t *)E1000_DMA_VADDR;
    nic.tx_ring = (struct E1000TxDesc *)(b + E1000_TXDESC_OFF);
    nic.rx_ring = (struct E1000RxDesc *)(b + E1000_RXDESC_OFF);
    nic.tx_bufs = b + E1000_TXBUF_OFF;
    nic.rx_bufs = b + E1000_RXBUF_OFF;
}

/* -----------------------------------------------------------------------
 * TX ring initialisation
 *
 * Register layout:
 *   TDBAL/TDBAH  64-bit physical address of the descriptor ring array
 *   TDLEN        ring length in bytes (must be 128-byte aligned)
 *   TDH          hardware-owned head pointer (do not write after init)
 *   TDT          software tail pointer; writing advances the ring
 *
 * TCTL configuration:
 *   EN   -- enable transmitter
 *   PSP  -- pad frames shorter than 64 bytes (required by 802.3)
 *   CT   -- collision threshold (16 attempts, value 0x10)
 *   COLD -- collision distance for full-duplex (0x40)
 *
 * TIPG (Inter-Packet Gap) for standard IEEE 802.3:
 *   IPGT=10, IPGR1=8, IPGR2=6  → TIPG = 0x0060_200A
 * ----------------------------------------------------------------------- */
static void
e1000_tx_init(void) {
    uintptr_t pa = get_phys_addr(nic.tx_ring);

    e1000_reg_write(E1000_TDBAL, (uint32_t)(pa & 0xFFFFFFFF));
    e1000_reg_write(E1000_TDBAH, (uint32_t)(pa >> 32));
    e1000_reg_write(E1000_TDLEN,
                    E1000_NUM_TX_DESC * (uint32_t)sizeof(struct E1000TxDesc));
    e1000_reg_write(E1000_TDH, 0);
    e1000_reg_write(E1000_TDT, 0);
    nic.tx_tail = 0;

    e1000_reg_write(E1000_TCTL,
        E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_CT | E1000_TCTL_COLD);
    e1000_reg_write(E1000_TIPG, 10u | (8u << 10) | (6u << 20));
}

/* -----------------------------------------------------------------------
 * RX ring initialisation
 *
 * Register layout:
 *   RAL0/RAH0    receive address filter: our MAC address + valid bit
 *   MTA          multicast table array (zeroed = no multicast)
 *   RDBAL/RDBAH  64-bit physical address of the descriptor ring array
 *   RDLEN        ring length in bytes
 *   RDH          hardware head (NIC advances this as frames arrive)
 *   RDT          software tail; we advance this to hand descriptors to NIC
 *
 * Each RX descriptor is pre-filled with the physical address of its 2 KB
 * buffer.  We give all N descriptors to the NIC by writing RDT = N-1.
 *
 * RCTL configuration:
 *   EN    -- enable receiver
 *   BAM   -- accept broadcast frames
 *   BSIZE -- 2048-byte buffers (BSIZE=00 with BSEX=0)
 *   SECRC -- strip the 4-byte Ethernet FCS before storing in memory
 * ----------------------------------------------------------------------- */
static void
e1000_rx_init(void) {
    /* Receive address filter: our MAC */
    uint32_t ral = (uint32_t)nic.mac[0]
                 | ((uint32_t)nic.mac[1] << 8)
                 | ((uint32_t)nic.mac[2] << 16)
                 | ((uint32_t)nic.mac[3] << 24);
    uint32_t rah = (uint32_t)nic.mac[4]
                 | ((uint32_t)nic.mac[5] << 8)
                 | E1000_RAH_AV;
    e1000_reg_write(E1000_RAL0, ral);
    e1000_reg_write(E1000_RAH0, rah);

    /* Disable multicast promiscuity */
    for (int i = 0; i < 128; i++)
        e1000_reg_write(E1000_MTA + (uint32_t)i * 4, 0);

    /* Point each descriptor at its dedicated receive buffer */
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        nic.rx_ring[i].addr   = get_phys_addr(nic.rx_bufs + i * E1000_PKT_SIZE);
        nic.rx_ring[i].status = 0;
    }

    uintptr_t pa = get_phys_addr(nic.rx_ring);
    e1000_reg_write(E1000_RDBAL, (uint32_t)(pa & 0xFFFFFFFF));
    e1000_reg_write(E1000_RDBAH, (uint32_t)(pa >> 32));
    e1000_reg_write(E1000_RDLEN,
                    E1000_NUM_RX_DESC * (uint32_t)sizeof(struct E1000RxDesc));
    e1000_reg_write(E1000_RDH, 0);
    e1000_reg_write(E1000_RDT, E1000_NUM_RX_DESC - 1);
    nic.rx_tail = E1000_NUM_RX_DESC - 1;

    e1000_reg_write(E1000_RCTL,
        E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_BSIZE | E1000_RCTL_SECRC);
}

/* -----------------------------------------------------------------------
 * e1000_init — public entry point
 *
 * Sequence:
 *   1. Locate the NIC on the PCI bus by class/subclass.
 *   2. Enable Bus Mastering so the NIC can DMA into RAM.
 *   3. Map BAR0 registers into our virtual address space.
 *   4. Issue a software reset to bring the device to a known state.
 *   5. Disable all interrupts (polling mode).
 *   6. Read the factory MAC address from EEPROM.
 *   7. Initialise TX and RX descriptor rings.
 * ----------------------------------------------------------------------- */
void
e1000_init(void) {
    nic.pcidev = find_pci_dev(E1000_PCI_CLASS, E1000_PCI_SUBCLASS);
    if (!nic.pcidev)
        panic("e1000: no Ethernet controller found on PCI bus\n");
    cprintf("e1000: found at PCI bus %d dev %d func %d"
            " (vendor=0x%04x device=0x%04x)\n",
            nic.pcidev->bus, nic.pcidev->device, nic.pcidev->function,
            nic.pcidev->vendor_id, nic.pcidev->device_id);

    pci_enable_busmaster(nic.pcidev);

    e1000_map();

    /* Software reset: self-clearing RST bit restores all register defaults */
    e1000_reg_write(E1000_CTRL, E1000_CTRL_RST);
    while (e1000_reg_read(E1000_CTRL) & E1000_CTRL_RST)
        ;

    /* Disable interrupts; clear any pending causes left by reset */
    e1000_reg_write(E1000_IMC, 0xFFFFFFFF);
    (void)e1000_reg_read(E1000_ICR);

    e1000_mac_read();
    cprintf("e1000: MAC = %02x:%02x:%02x:%02x:%02x:%02x\n",
            nic.mac[0], nic.mac[1], nic.mac[2],
            nic.mac[3], nic.mac[4], nic.mac[5]);

    e1000_alloc_dma();
    e1000_tx_init();
    e1000_rx_init();

    uint32_t status = e1000_reg_read(E1000_STATUS);
    cprintf("e1000: init done  link=%s  STATUS=0x%08x\n",
            (status & E1000_STATUS_LU) ? "up" : "down", status);
}

/* -----------------------------------------------------------------------
 * e1000_send — transmit one Ethernet frame (polling)
 *
 * How it works
 * ------------
 * The TX ring is a circular array of 16 descriptors.  nic.tx_tail tracks
 * the next slot the driver should fill.  A slot is free when:
 *   - It has never been used (status == 0, cmd == 0 — true after reset), or
 *   - The NIC has finished transmitting it (DD bit set in status).
 *
 * The driver:
 *   1. Checks the slot at tx_tail for availability.
 *   2. Copies the frame into the associated packet buffer.
 *   3. Fills the descriptor: physical address, length, command flags.
 *   4. Clears status so we can detect completion of this send later.
 *   5. Advances TDT by one — this is the doorbell that wakes the NIC.
 *
 * Returns 0 on success, -1 if the ring is full (caller retries).
 * ----------------------------------------------------------------------- */
int
e1000_send(const void *buf, size_t len) {
    if (len > E1000_PKT_SIZE)
        return -1;

    uint32_t tail          = nic.tx_tail;
    struct E1000TxDesc *d  = &nic.tx_ring[tail];
    uint8_t *txbuf         = nic.tx_bufs + tail * E1000_PKT_SIZE;

    /* Slot is free only if unused OR NIC has set DD after previous send */
    if (d->cmd != 0 && !(d->status & E1000_TXD_STAT_DD))
        return -1; /* ring full */

    memcpy(txbuf, buf, len);

    d->addr    = get_phys_addr(txbuf);
    d->length  = (uint16_t)len;
    d->cso     = 0;
    d->cmd     = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS;
    d->status  = 0; /* clear DD so the next send can check this slot */
    d->css     = 0;
    d->special = 0;

    nic.tx_tail = (tail + 1) % E1000_NUM_TX_DESC;
    e1000_reg_write(E1000_TDT, nic.tx_tail); /* doorbell: NIC starts TX */
    return 0;
}

/* -----------------------------------------------------------------------
 * e1000_recv — receive one Ethernet frame (non-blocking poll)
 *
 * How it works
 * ------------
 * nic.rx_tail is the last descriptor index we handed to the NIC (via RDT).
 * The NIC fills descriptors starting at (rx_tail + 1) % N and advances RDH.
 * We check the next expected descriptor: if its DD bit is set a frame has
 * arrived.
 *
 * After consuming a frame we:
 *   - Copy the data out of the NIC-owned buffer.
 *   - Reset the status byte so the NIC can reuse the descriptor.
 *   - Advance RDT to hand the slot back to the NIC.
 *
 * Returns 0 and sets *len on success; returns -1 if no frame is waiting.
 * ----------------------------------------------------------------------- */
int
e1000_recv(void *buf, size_t *len) {
    uint32_t next           = (nic.rx_tail + 1) % E1000_NUM_RX_DESC;
    struct E1000RxDesc *d   = &nic.rx_ring[next];

    if (!(d->status & E1000_RXD_STAT_DD))
        return -1; /* no frame available */

    *len = d->length;
    memcpy(buf, nic.rx_bufs + next * E1000_PKT_SIZE, *len);

    /* Return descriptor to NIC */
    d->status  = 0;
    nic.rx_tail = next;
    e1000_reg_write(E1000_RDT, nic.rx_tail);
    return 0;
}

/* -----------------------------------------------------------------------
 * e1000_get_mac — query the NIC hardware MAC address
 * ----------------------------------------------------------------------- */
void
e1000_get_mac(uint8_t mac[6]) {
    memcpy(mac, nic.mac, 6);
}

/* -----------------------------------------------------------------------
 * e1000_test — smoke test: send a broadcast frame, poll for any reply
 *
 * In user-mode QEMU networking, outbound frames are visible but inbound
 * frames only arrive if the host actively sends something (e.g. nc -u).
 * With a TAP device use tshark/Wireshark on the tap interface to observe
 * the outgoing frame.
 * ----------------------------------------------------------------------- */
void
e1000_test(void) {
    static uint8_t frame[64];
    memset(frame, 0, sizeof(frame));

    /* Ethernet header */
    memset(frame, 0xFF, 6);              /* dst: broadcast */
    memcpy(frame + 6, nic.mac, 6);       /* src: our MAC */
    frame[12] = 0x08; frame[13] = 0x00;  /* EtherType: 0x0800 (IPv4-like) */

    /* Payload: human-readable marker */
    const char *marker = "JOS e1000 driver test";
    memcpy(frame + 14, marker, strlen(marker));

    if (e1000_send(frame, sizeof(frame)) < 0) {
        cprintf("e1000_test: send failed (TX ring full?)\n");
        return;
    }
    cprintf("e1000_test: test frame sent (64 bytes, dst=broadcast)\n");

    /* Poll briefly for any inbound frame */
    uint8_t rxbuf[E1000_PKT_SIZE];
    size_t  rxlen = 0;
    for (int i = 0; i < 200000; i++) {
        if (e1000_recv(rxbuf, &rxlen) == 0) {
            cprintf("e1000_test: received %zu-byte frame  first 32 bytes:", rxlen);
            size_t show = rxlen < 32 ? rxlen : 32;
            for (size_t j = 0; j < show; j++)
                cprintf(" %02x", rxbuf[j]);
            cprintf("\n");
            return;
        }
    }
    cprintf("e1000_test: no frame received "
            "(expected without inbound host traffic)\n");
}
