// Pinos do encoder no Nice!Nano (P1.13 e P1.15)
const int pinA = 45;  // P1.13
const int pinB = 47;  // P1.15

const int led = LED_BUILTIN;

volatile long posicao = 0;
volatile byte ultimoEstado = 0;

void setup() {
  pinMode(pinA, INPUT_PULLUP);
  pinMode(pinB, INPUT_PULLUP);
  pinMode(led, OUTPUT);

  ultimoEstado = (digitalRead(pinA) << 1) | digitalRead(pinB);

  attachInterrupt(digitalPinToInterrupt(pinA), lerEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinB), lerEncoder, CHANGE);

  // Pisca 3x indicando que iniciou
  for (int i = 0; i < 3; i++) {
    digitalWrite(led, HIGH); delay(100);
    digitalWrite(led, LOW);  delay(100);
  }
}

void loop() {
  static long ultimaPos = 0;

  if (ultimaPos != posicao) {
    ultimaPos = posicao;
    // Pisca LED a cada passo
    digitalWrite(led, HIGH);
    delay(50);
    digitalWrite(led, LOW);
  }
}

void lerEncoder() {
  byte estado = (digitalRead(pinA) << 1) | digitalRead(pinB);
  byte transicao = (ultimoEstado << 2) | estado;

  switch (transicao) {
    case 0b0001: case 0b0111:
    case 0b1110: case 0b1000:
      posicao++;
      break;
    case 0b0010: case 0b0100:
    case 0b1101: case 0b1011:
      posicao--;
      break;
  }

  ultimoEstado = estado;
}
