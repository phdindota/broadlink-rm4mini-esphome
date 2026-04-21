Import("env")
import os

# LibreTiny digitalRead() patch for RTL8720CF IR receiver support
#
# Problem: ESPHome's remote_receiver uses attachInterrupt() which sets the
# PIN_IRQ flag on the pin. But digitalRead() checks for PIN_GPIO only via
# pinCheckGetData(). When called from the ISR, it fails the check and
# returns LOW without reading the pin — so no IR codes are ever decoded.
#
# Fix: Allow digitalRead() to work on pins with PIN_IRQ set, and use
# gpio_irq_read() when an IRQ context is active on the pin.

platform_dir = env.PioPlatform().get_dir()
target = None
for root, dirs, files in os.walk(platform_dir):
    if "wiring_digital.c" in files and "realtek" in root.lower():
        target = os.path.join(root, "wiring_digital.c")
        break

if not target:
    print("*** PATCH: wiring_digital.c NOT FOUND ***")
else:
    with open(target, "r") as f:
        content = f.read()

    if "PIN_GPIO | PIN_IRQ" in content:
        print("*** PATCH: Already patched ***")
    elif "gpio_irq_read" in content:
        # Old version of patch (checks irq but not PIN_IRQ flag) — replace
        old = """PinStatus digitalRead(pin_size_t pinNumber) {
\tpinCheckGetData(pinNumber, PIN_GPIO, LOW);
\tpinSetInputMode(pin, data, pinNumber);
\t// Patched: use gpio_irq_read in ISR context for remote_receiver
\tif (data->irq) {
\t\treturn gpio_irq_read(data->irq);
\t}
\treturn gpio_read(data->gpio);
}"""
        new = """PinStatus digitalRead(pin_size_t pinNumber) {
\tpinCheckGetData(pinNumber, PIN_GPIO | PIN_IRQ, LOW);
\tif (data->irq) {
\t\treturn gpio_irq_read(data->irq);
\t}
\tpinSetInputMode(pin, data, pinNumber);
\treturn gpio_read(data->gpio);
}"""
        content = content.replace(old, new)
        with open(target, "w") as f:
            f.write(content)
        print("*** PATCH: Upgraded old patch to PIN_IRQ version ***")
    else:
        # Fresh LibreTiny — apply patch from scratch
        old = """PinStatus digitalRead(pin_size_t pinNumber) {
\tpinCheckGetData(pinNumber, PIN_GPIO, LOW);
\tpinSetInputMode(pin, data, pinNumber);
\treturn gpio_read(data->gpio);
}"""
        new = """PinStatus digitalRead(pin_size_t pinNumber) {
\tpinCheckGetData(pinNumber, PIN_GPIO | PIN_IRQ, LOW);
\tif (data->irq) {
\t\treturn gpio_irq_read(data->irq);
\t}
\tpinSetInputMode(pin, data, pinNumber);
\treturn gpio_read(data->gpio);
}"""
        if old in content:
            content = content.replace(old, new)
            with open(target, "w") as f:
                f.write(content)
            print("*** PATCH: Applied gpio_irq_read + PIN_IRQ patch ***")
        else:
            print("*** PATCH: Could not match digitalRead function — manual patch required ***")
            print("*** PATCH: Expected to find: ***")
            for line in old.split("\n"):
                print(f"  {line}")
