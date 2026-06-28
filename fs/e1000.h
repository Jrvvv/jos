#ifndef E1000_H
#define E1000_H

#include "pci.h"

/* -----------------------------------------------------------------------
 * Virtual address layout for the e1000 driver (extends pci.h ranges).
 *
 * ECAM          0x7000000000  (defined in pci.h)
 * NVME MMIO     0x7010000000  (defined in pci.h)
 * NVME queues   0x7020000000  (defined in pci.h)
 * E1000 MMIO    0x7030000000  <-- NIC register space (BAR0, ~128 KB)
 * E1000 DMA     0x7040000000  <-- descriptor rings + packet buffers
 * ----------------------------------------------------------------------- */
#define E1000_MMIO_VADDR 0x7030000000ULL
#define E1000_DMA_VADDR  0x7040000000ULL

/* PCI identifiers for Intel 82540EM (the default QEMU e1000 model) */
#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100E
#define E1000_PCI_CLASS    0x02  /* Network controller */
#define E1000_PCI_SUBCLASS 0x00  /* Ethernet */

/* Maximum BAR0 size we map (128 KB is enough for all registers) */
#define E1000_MMIO_SIZE 0x20000

/* -----------------------------------------------------------------------
 * e1000 register offsets (from BAR0 base)
 * Reference: Intel 82540EM Software Developer Manual, section 13.
 * ----------------------------------------------------------------------- */
#define E1000_CTRL   0x00000  /* Device Control */
#define E1000_STATUS 0x00008  /* Device Status */
#define E1000_EERD   0x00014  /* EEPROM Read */
#define E1000_ICR    0x000C0  /* Interrupt Cause Read */
#define E1000_ICS    0x000C8  /* Interrupt Cause Set */
#define E1000_IMS    0x000D0  /* Interrupt Mask Set/Read */
#define E1000_IMC    0x000D8  /* Interrupt Mask Clear */
#define E1000_RCTL   0x00100  /* Receive Control */
#define E1000_TCTL   0x00400  /* Transmit Control */
#define E1000_TIPG   0x00410  /* Transmit Inter-Packet Gap */
#define E1000_RDBAL  0x02800  /* RX Descriptor Base Address Low */
#define E1000_RDBAH  0x02804  /* RX Descriptor Base Address High */
#define E1000_RDLEN  0x02808  /* RX Descriptor Ring Length (bytes) */
#define E1000_RDH    0x02810  /* RX Descriptor Head (hardware-owned) */
#define E1000_RDT    0x02818  /* RX Descriptor Tail (software-owned) */
#define E1000_TDBAL  0x03800  /* TX Descriptor Base Address Low */
#define E1000_TDBAH  0x03804  /* TX Descriptor Base Address High */
#define E1000_TDLEN  0x03808  /* TX Descriptor Ring Length (bytes) */
#define E1000_TDH    0x03810  /* TX Descriptor Head */
#define E1000_TDT    0x03818  /* TX Descriptor Tail */
#define E1000_MTA    0x05200  /* Multicast Table Array (128 x 32-bit) */
#define E1000_RAL0   0x05400  /* Receive Address Low  (MAC bytes 0-3) */
#define E1000_RAH0   0x05404  /* Receive Address High (MAC bytes 4-5 + Valid) */

/* CTRL register bits */
#define E1000_CTRL_RST  (1u << 26)  /* Software reset (self-clearing) */

/* STATUS register bits */
#define E1000_STATUS_LU (1u << 1)   /* Link Up */

/* EERD register fields */
#define E1000_EERD_START (1u << 0)   /* Start read */
#define E1000_EERD_DONE  (1u << 4)   /* Read done */
#define E1000_EERD_ADDR_SHIFT 8
#define E1000_EERD_DATA_SHIFT 16

/* RCTL register bits */
#define E1000_RCTL_EN    (1u << 1)   /* Receiver Enable */
#define E1000_RCTL_BAM   (1u << 15)  /* Broadcast Accept Mode */
#define E1000_RCTL_BSIZE (0u << 16)  /* Buffer size 2048 (BSIZE=00, BSEX=0) */
#define E1000_RCTL_SECRC (1u << 26)  /* Strip Ethernet CRC */

/* TCTL register bits */
#define E1000_TCTL_EN   (1u << 1)    /* Transmit Enable */
#define E1000_TCTL_PSP  (1u << 3)    /* Pad Short Packets */
#define E1000_TCTL_CT   (0x10u << 4) /* Collision Threshold (recommended) */
#define E1000_TCTL_COLD (0x40u << 12)/* Collision Distance (full-duplex) */

/* RAH0 Valid bit */
#define E1000_RAH_AV (1u << 31)

/* TX descriptor CMD byte bits */
#define E1000_TXD_CMD_EOP  (1u << 0)  /* End of Packet */
#define E1000_TXD_CMD_IFCS (1u << 1)  /* Insert FCS (CRC) */
#define E1000_TXD_CMD_RS   (1u << 3)  /* Report Status (set DD on completion) */

/* TX/RX descriptor status byte bits */
#define E1000_TXD_STAT_DD (1u << 0)   /* Descriptor Done */
#define E1000_RXD_STAT_DD (1u << 0)   /* Descriptor Done */
#define E1000_RXD_STAT_EOP (1u << 1)  /* End of Packet */

/* -----------------------------------------------------------------------
 * Descriptor ring dimensions.
 *
 * 16 descriptors is small but sufficient for an educational driver.
 * Packet buffers are 2048 bytes (enough for any standard Ethernet frame
 * including 802.1Q tagging; the NIC strips the 4-byte FCS via RCTL.SECRC).
 * ----------------------------------------------------------------------- */
#define E1000_NUM_TX_DESC 16
#define E1000_NUM_RX_DESC 16
#define E1000_PKT_SIZE    2048

/* -----------------------------------------------------------------------
 * DMA memory layout (offsets from E1000_DMA_VADDR).
 *
 * The layout is designed so that each section starts on a 4 KB page
 * boundary, which simplifies physical-address queries via uvpt.
 *
 *   +0x0000  TX descriptors  16 × 16 B = 256 B  (fits in one page)
 *   +0x1000  RX descriptors  16 × 16 B = 256 B  (fits in one page)
 *   +0x2000  TX packet buffers  16 × 2048 B = 32 KB  (8 pages)
 *   +0xA000  RX packet buffers  16 × 2048 B = 32 KB  (8 pages)
 *   Total: 18 pages = 72 KB
 * ----------------------------------------------------------------------- */
#define E1000_DMA_PAGES      18
#define E1000_TXDESC_OFF     0x0000
#define E1000_RXDESC_OFF     0x1000
#define E1000_TXBUF_OFF      0x2000
#define E1000_RXBUF_OFF      0xA000

/* -----------------------------------------------------------------------
 * Hardware descriptor structures.
 * Both are exactly 16 bytes as required by the 82540EM spec.
 * ----------------------------------------------------------------------- */

/* Legacy TX descriptor (section 3.3.3 of the datasheet) */
struct E1000TxDesc {
    uint64_t addr;    /* Physical address of packet buffer */
    uint16_t length;  /* Packet length in bytes */
    uint8_t  cso;     /* Checksum offset (0 when not inserting checksum) */
    uint8_t  cmd;     /* Command bits: EOP, IFCS, RS */
    uint8_t  status;  /* Status bits: DD set by NIC when done */
    uint8_t  css;     /* Checksum start (0 when unused) */
    uint16_t special;
} __attribute__((packed));

/* Legacy RX descriptor (section 3.2.4) */
struct E1000RxDesc {
    uint64_t addr;      /* Physical address of receive buffer (set by driver) */
    uint16_t length;    /* Number of bytes received (set by NIC) */
    uint16_t checksum;  /* Packet checksum (set by NIC) */
    uint8_t  status;    /* Status bits: DD, EOP (set by NIC) */
    uint8_t  errors;    /* Error bits (set by NIC) */
    uint16_t special;
} __attribute__((packed));

/* -----------------------------------------------------------------------
 * Driver state
 * ----------------------------------------------------------------------- */
struct E1000 {
    struct PciDevice *pcidev;

    volatile uint8_t *mmio;  /* Virtual address of BAR0 MMIO registers */
    uint8_t mac[6];          /* NIC MAC address read from EEPROM */

    struct E1000TxDesc *tx_ring;  /* Virtual address of TX descriptor ring */
    struct E1000RxDesc *rx_ring;  /* Virtual address of RX descriptor ring */

    uint8_t *tx_bufs;  /* Virtual base of TX packet buffer pool */
    uint8_t *rx_bufs;  /* Virtual base of RX packet buffer pool */

    uint32_t tx_tail;  /* Next TX descriptor index to fill */
    uint32_t rx_tail;  /* Last RX descriptor index given to NIC */
};

/* -----------------------------------------------------------------------
 * Public interface (used by the network stack in Part 2)
 * ----------------------------------------------------------------------- */

/* Initialise the NIC.  Panics if no e1000 device is found on the PCI bus. */
void e1000_init(void);

/* Copy buf[0..len-1] into a free TX descriptor and kick the NIC.
 * Returns  0 on success, -1 if the TX ring is full (caller should retry). */
int e1000_send(const void *buf, size_t len);

/* If a received frame is waiting, copy it to buf and set *len to its size.
 * Returns  0 on success, -1 if no frame is available (non-blocking poll). */
int e1000_recv(void *buf, size_t *len);

/* Fill mac[0..5] with the NIC's hardware MAC address. */
void e1000_get_mac(uint8_t mac[6]);

/* Smoke test: send a broadcast frame and poll briefly for any reply.
 * Called once during FS server initialisation to verify the driver works. */
void e1000_test(void);

#endif /* E1000_H */
