#include <Arduino.h>

// defined in light_example.c
extern "C" void light_example_app_main();

void setup() {
    // Serial.begin(115200); don't do that, "UART driver already installed"
    light_example_app_main();
}

void loop() {
    delay(1000); // nothing to do, work happens in created tasks.
}