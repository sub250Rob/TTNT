#include <Arduino.h>
#include <stdio.h>
#include "serial_shim.h"
// Hi

// put function declarations here:
int myFunction(int, int);

void setup() {
  // put your setup code here, to run once:
  serial_begin(9600);

  int result = myFunction(2, 3);
  printf("Hello world\n");
  printf("%d\n", result);
}

void loop() {
  // put your main code here, to run repeatedly:
  printf("tick %lu\n", millis());
  delay(1000);
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}
