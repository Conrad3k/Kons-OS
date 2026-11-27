/* K1 — host test: an input device as nadzorca sees it in sysfs (src/wspolne/k1_urz_wejscia.c; 0.13.0 step 5, ADR 0060).
 * The bitmaps as the kernel writes them (input_print_bitmap: words of the most significant first, leading zeros cut) —
 * of QEMU's USB keyboard, mouse and tablet, a laptop's touchpad, a power button; then broken ones refused; the USB paths
 * of sysfs against PS/2 and I2C ones; the mark of a device. */
#include <string.h>

#include "k1_test_host.h"
#include "k1_urz_wejscia.h"

/* QEMU "usb-kbd" (input/event of hid-generic): ev 120013, key with letters, the space bar, the LEDs */
static const struct k1_urz_bity KLAWIATURA = { "120013\n", "1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe\n",
                                               "0\n", "0\n", "0\n" };
/* QEMU "usb-mouse": ev 17 (SYN KEY REL MSC), BTN_LEFT..BTN_MIDDLE at 0x110, rel X Y WHEEL */
static const struct k1_urz_bity MYSZ = { "17\n", "70000 0 0 0 0\n", "103\n", "0\n", "0\n" };
/* QEMU "usb-tablet": absolute pointer without fingers — not a mouse nor a touchpad for K1 (wejscie does not keep it) */
static const struct k1_urz_bity TABLET = { "1f\n", "70000 0 0 0 0\n", "100\n", "3\n", "0\n" };
/* a touchpad (SynPS/2): ABS_X, ABS_Y, BTN_TOUCH (0x14a), BTN_TOOL_FINGER (0x145), INPUT_PROP_POINTER */
static const struct k1_urz_bity TOUCHPAD = { "b\n", "e520 10000 0 0 0 0\n", "0\n", "660800011000003\n", "5\n" };
/* the same with INPUT_PROP_DIRECT: a touch screen */
static const struct k1_urz_bity EKRAN_DOTYK = { "b\n", "e520 10000 0 0 0 0\n", "0\n", "660800011000003\n", "2\n" };
/* the power button: ev 3, KEY_POWER (116) only */
static const struct k1_urz_bity ZASILANIE = { "3\n", "10000000000000 0\n", "0\n", "0\n", "0\n" };

static void bity(void)
{
    EXPECT(k1_urz_bit("120013\n", 1) == 1 && k1_urz_bit("120013\n", 2) == 0 && k1_urz_bit("120013\n", 20) == 1);
    EXPECT(k1_urz_bit("1 0\n", 64) == 1 && k1_urz_bit("1 0\n", 0) == 0 && k1_urz_bit("1 0\n", 700) == 0);
    EXPECT(k1_urz_bit("ffffffffffffffff\n", 63) == 1 && k1_urz_bit("FF", 7) == 1 && k1_urz_bit("0", 0) == 0);
    EXPECT(k1_urz_bit(NULL, 3) == 0);
    /* NEGATIVE: not a bitmap of sysfs */
    EXPECT(k1_urz_bit("", 0) == -1);
    EXPECT(k1_urz_bit("\n", 0) == -1);
    EXPECT(k1_urz_bit("12 x3\n", 0) == -1);
    EXPECT(k1_urz_bit("12  3\n", 0) == -1);
    EXPECT(k1_urz_bit(" 12\n", 0) == -1);
    EXPECT(k1_urz_bit("12 \n", 0) == -1);
    EXPECT(k1_urz_bit("12\n3", 0) == -1);
    EXPECT(k1_urz_bit("1ffffffffffffffff\n", 0) == -1); /* 17 digits */
    char dlugi[33 * 2 + 1];
    for (int i = 0; i < 33; i++)
        dlugi[2 * i] = '1', dlugi[2 * i + 1] = ' ';
    dlugi[65] = '\0';
    EXPECT(k1_urz_bit(dlugi, 0) == -1); /* 33 words */
    dlugi[63] = '\0';
    EXPECT(k1_urz_bit(dlugi, 0) == 1 && k1_urz_bit(dlugi, 31 * 64) == 1); /* 32 words */
}

static void rodzaje(void)
{
    struct k1_urz_bity zle = KLAWIATURA;
    EXPECT(k1_urz_rodzaj(&KLAWIATURA) == K1_URZ_KLAWIATURA);
    EXPECT(k1_urz_rodzaj(&MYSZ) == K1_URZ_MYSZ);
    EXPECT(k1_urz_rodzaj(&TOUCHPAD) == K1_URZ_MYSZ);
    EXPECT(k1_urz_rodzaj(&TABLET) == K1_URZ_INNE);
    EXPECT(k1_urz_rodzaj(&EKRAN_DOTYK) == K1_URZ_INNE);
    EXPECT(k1_urz_rodzaj(&ZASILANIE) == K1_URZ_INNE);
    /* a keyboard that also moves the pointer (a BadUSB "mouse"): a keyboard for the question */
    zle.rel = "103\n";
    zle.ev = "120017\n";
    EXPECT(k1_urz_rodzaj(&zle) == K1_URZ_KLAWIATURA);
    /* NEGATIVE: no files, broken files — never a keyboard nor a mouse by mistake, "inne" (it still waits on USB) */
    struct k1_urz_bity brak = { NULL, NULL, NULL, NULL, NULL };
    EXPECT(k1_urz_rodzaj(&brak) == K1_URZ_INNE);
    zle = KLAWIATURA;
    zle.key = "zle\n";
    EXPECT(k1_urz_rodzaj(&zle) == K1_URZ_INNE);
    zle = MYSZ;
    zle.ev = "13\n"; /* EV_REL (bit 2) missing */
    EXPECT(k1_urz_rodzaj(&zle) == K1_URZ_INNE);
}

static void sciezki(void)
{
    EXPECT(k1_urz_na_usb("../../devices/pci0000:00/0000:00:14.0/usb1/1-2/1-2:1.0/0003:046D:C52B.0001/input/input7/event7"));
    EXPECT(k1_urz_na_usb("../../devices/pci0000:00/0000:00:1d.0/usb2/2-1/2-1.4/2-1.4:1.1/0003:0627:0001.0002/input/input9/event9"));
    EXPECT(!k1_urz_na_usb("../../devices/platform/i8042/serio0/input/input3/event3"));
    EXPECT(!k1_urz_na_usb("../../devices/pci0000:00/0000:00:15.1/i2c_designware.1/i2c-1/i2c-ELAN0001:00/0018:04F3:3140.0001/input/input12/event12"));
    EXPECT(!k1_urz_na_usb("../../devices/LNXSYSTM:00/LNXPWRBN:00/input/input0/event0"));
    EXPECT(!k1_urz_na_usb("../../devices/virtual/input/usbfake/event4")); /* "/usb" without a bus number */
    EXPECT(!k1_urz_na_usb(""));
}

static void znaki(void)
{
    uint32_t a = k1_urz_znak("0627", "0001", "QEMU QEMU USB Mouse");
    EXPECT(a != 0 && a == k1_urz_znak("0627\n", "0001\n", "QEMU QEMU USB Mouse\n"));
    EXPECT(a != k1_urz_znak("0627", "0001", "QEMU QEMU USB Keyboard"));
    EXPECT(a != k1_urz_znak("0627", "0002", "QEMU QEMU USB Mouse"));
    EXPECT(k1_urz_znak("06", "270001", "QEMU QEMU USB Mouse") != a); /* the parts are separated */
    EXPECT(k1_urz_znak(NULL, NULL, NULL) != 0 && k1_urz_znak("", "", "") != 0);
    char dlugi[300];
    memset(dlugi, 'x', sizeof dlugi - 1);
    dlugi[sizeof dlugi - 1] = '\0';
    EXPECT(k1_urz_znak("1", "2", dlugi) == k1_urz_znak("1", "2", dlugi + 100)); /* the name cut at 128 bytes */
}

int main(void)
{
    bity();
    rodzaje();
    sciezki();
    znaki();
    return K1_TEST_KONIEC("test_urz_wejscia");
}
