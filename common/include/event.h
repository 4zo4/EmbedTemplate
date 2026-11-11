#pragma once

#ifndef EVT_BITMAP
#define EVT_BITMAP uint32_t // 32-bit bitmap for event notifications
#endif
#define EVT_SYS_TICK BIT(0)
#define EVT_DATA_READY BIT(1)
#define EVT_PCI_INT_IDX(index) BIT(2 + index)
#define EVT_PCI_INT_MASK 0x3FC // Bits 2-9 for PCI Int events (up to 8 vectors)
#define EVT_PCI_INT_0 BIT(2)
#define EVT_PCI_INT_1 BIT(3)
#define EVT_PCI_INT_2 BIT(4)
#define EVT_PCI_INT_3 BIT(5)
#define EVT_PCI_INT_4 BIT(6)
#define EVT_PCI_INT_5 BIT(7)
#define EVT_PCI_INT_6 BIT(8)
#define EVT_PCI_INT_7 BIT(9)
#define EVT_PCI_TEST BIT(10)

extern volatile EVT_BITMAP event_notify;
