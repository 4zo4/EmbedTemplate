import gdb

class DumpCom1(gdb.Command):
    """Dumps all 8 bytes of the COM1 serial port using the QEMU monitor."""
    def __init__(self):
        super(DumpCom1, self).__init__("dump_com1", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        registers = [
            "RBR/THR (Data Buffer)",
            "IER (Interrupt Enable)",
            "IIR (Interrupt Ident)",
            "LCR (Line Control)",
            "MCR (Modem Control)",
            "LSR (Line Status)",
            "MSR (Modem Status)",
            "SCR (Scratch Register)"
        ]

        print("\n=== COM1 PORT REGISTER DUMP ===")
        for i, reg_name in enumerate(registers):
            port = 0x3F8 + i
            raw_res = gdb.execute(f"monitor i/b {port}", to_string=True)
            clean_res = raw_res.strip()
            print(f"Port 0x{port:03X} | {reg_name}: {clean_res}")

DumpCom1()
