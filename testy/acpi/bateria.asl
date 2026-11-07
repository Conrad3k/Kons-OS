/* K1 tests (0.5.3): a laptop battery and an AC adapter for QEMU, which has none.
 * QEMU adds this table to the machine (-acpitable file=...aml); the kernel's ACPI battery and AC
 * drivers then see them like on a real laptop (sysfs power_supply BAT0, AC).
 * Placeholders replaced by the test scripts: @STAN@ = _BST state (1 discharging, 2 charging),
 * @ZASILACZ@ = AC adapter online (0/1). Charge: 32850 of 45000 mWh = 73 %. */
DefinitionBlock ("", "SSDT", 2, "K1", "BATERIA", 1)
{
    Scope (\_SB)
    {
        Device (BAT0)
        {
            Name (_HID, EisaId ("PNP0C0A"))
            Name (_UID, One)
            Method (_STA, 0, NotSerialized) { Return (0x1F) }
            Method (_BIF, 0, NotSerialized)
            {
                Return (Package (0x0D) { Zero, 50000, 45000, One, 11100, 4500, 2000, 100, 100,
                                         "K1 TEST", "1", "LION", "K1" })
            }
            Method (_BST, 0, NotSerialized) { Return (Package (0x04) { @STAN@, 9000, 32850, 11500 }) }
        }
        Device (AC)
        {
            Name (_HID, "ACPI0003")
            Method (_STA, 0, NotSerialized) { Return (0x0F) }
            Method (_PSR, 0, NotSerialized) { Return (@ZASILACZ@) }
        }
    }
}
