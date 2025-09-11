#include <MD_MAX72xx.h>
#include <SPI.h>

// === Konfigurasi Display ===
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4  // 4 modul 8x8 = 32x8
#define DATA_PIN   11  // DIN
#define CLK_PIN    13  // CLK
#define CS_PIN     10  // CS

MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// === Konfigurasi Joystick ===
#define VRX A0
#define VRY A1
#define SW  2   // tombol joystick (rotate)

// Ukuran arena
const uint8_t PLAY_W = 8;
const uint8_t PLAY_H = 32;

uint8_t board[PLAY_H][PLAY_W];
uint8_t devBuf[MAX_DEVICES][8];

int score = 0;
bool gameOver = false;

// Tetrimino (7 buah, 4 rotasi)
const uint16_t pieces[7][4] = {
  { 0x0F00, 0x2222, 0x0F00, 0x2222 }, // I
  { 0x8E00, 0x6440, 0x0E20, 0x44C0 }, // J
  { 0x2E00, 0x4460, 0x0E80, 0xC440 }, // L
  { 0x6600, 0x6600, 0x6600, 0x6600 }, // O
  { 0x6C00, 0x4620, 0x6C00, 0x4620 }, // S
  { 0x4E00, 0x4640, 0x0E40, 0x4C40 }, // T
  { 0xC600, 0x2640, 0xC600, 0x2640 }  // Z
};

struct ActivePiece {
  int x, y;
  uint8_t type, rot;
} piece;

unsigned long lastDrop = 0;
unsigned long dropInterval = 600;

bool btnPressedLast = false;
unsigned long dropBtnPressTime = 0;

// === Helper fungsi ===
bool pieceBit(uint16_t pat, int px, int py) {
  int idx = py * 4 + px;
  return (pat & (1 << (15 - idx))) != 0;
}

bool canPlace(int x, int y, uint8_t t, uint8_t r) {
  uint16_t pat = pieces[t][r % 4];
  for (int py = 0; py < 4; py++) {
    for (int px = 0; px < 4; px++) {
      if (!pieceBit(pat, px, py)) continue;
      int bx = x + px;
      int by = y + py;
      if (bx < 0 || bx >= PLAY_W) return false;
      if (by >= PLAY_H) return false;
      if (by >= 0 && board[by][bx]) return false;
    }
  }
  return true;
}

void spawnPiece() {
  piece.type = random(0, 7);
  piece.rot = 0;
  piece.x = 2;
  piece.y = -3;
  if (!canPlace(piece.x, piece.y, piece.type, piece.rot)) {
    gameOver = true;
  }
}

void clearLines() {
  for (int y = PLAY_H - 1; y >= 0; y--) {
    bool full = true;
    for (int x = 0; x < PLAY_W; x++) {
      if (!board[y][x]) {
        full = false;
        break;
      }
    }
    if (full) {
      score++;
      for (int yy = y; yy > 0; yy--) {
        for (int x = 0; x < PLAY_W; x++) {
          board[yy][x] = board[yy - 1][x];
        }
      }
      for (int x = 0; x < PLAY_W; x++) board[0][x] = 0;
      y++; // cek ulang baris ini setelah shift
    }
  }
}

void lockPiece() {
  uint16_t pat = pieces[piece.type][piece.rot % 4];
  for (int py = 0; py < 4; py++) {
    for (int px = 0; px < 4; px++) {
      if (!pieceBit(pat, px, py)) continue;
      int bx = piece.x + px;
      int by = piece.y + py;
      if (by >= 0 && by < PLAY_H && bx >= 0 && bx < PLAY_W) {
        board[by][bx] = 1;
      }
      // jika ada blok terkunci di atas layar → game over
      if (by < 0) {
        gameOver = true;
      }
    }
  }
  if (!gameOver) {
    clearLines();
    spawnPiece();
  }
}

void clearDevBuf() {
  for (int d = 0; d < MAX_DEVICES; d++) {
    for (int r = 0; r < 8; r++) devBuf[d][r] = 0;
  }
}

// === Mapping untuk modul 4-in-1 horizontal (32x8), game vertikal 8x32 ===
void setPixelMapped(int x, int y) {
  if (x < 0 || x >= PLAY_W || y < 0 || y >= PLAY_H) return;

  int newX = y;        // tinggi jadi lebar
  int newY = 7 - x;    // lebar jadi tinggi, dibalik supaya (0,0) di kiri-atas

  int device = newX / 8;   // modul ke berapa
  int col    = newX % 8;
  int row    = newY;

  devBuf[device][row] |= (1 << col);
}

void drawBoard() {
  clearDevBuf();

  // board permanen
  for (int y = 0; y < PLAY_H; y++) {
    for (int x = 0; x < PLAY_W; x++) {
      if (board[y][x]) setPixelMapped(x, y);
    }
  }

  // piece aktif
  uint16_t pat = pieces[piece.type][piece.rot % 4];
  for (int py = 0; py < 4; py++) {
    for (int px = 0; px < 4; px++) {
      if (!pieceBit(pat, px, py)) continue;
      int bx = piece.x + px;
      int by = piece.y + py;
      if (by >= 0) setPixelMapped(bx, by);
    }
  }

  // kirim ke modul
  for (int d = 0; d < MAX_DEVICES; d++) {
    for (int r = 0; r < 8; r++) {
      mx.setRow(d, r, devBuf[d][r]);
    }
  }
}

void hardDrop() {
  while (canPlace(piece.x, piece.y + 1, piece.type, piece.rot)) {
    piece.y++;
  }
  lockPiece();
}

// === Score Display ===
uint8_t flipByte(uint8_t b) {
  uint8_t rev = 0;
  for (int i = 0; i < 8; i++) {
    if (b & (1 << i)) rev |= (1 << (7 - i));
  }
  return rev;
}

void showScore() {
  mx.clear();
  String txt = String(score);
  uint8_t cBuf[8];
  int pos = 0;

  for (int i = 0; i < txt.length(); i++) {
    char c = txt[i];
    int width = mx.getChar(c, sizeof(cBuf), cBuf);

    for (int col = 0; col < width; col++) {
      mx.setColumn(pos++, cBuf[col]);
    }
    mx.setColumn(pos++, 0); // spasi antar digit
  }

  mx.update();
}

// === Tampilan teks statis (misal "TETRIS") ===
void showStaticText(const char *txt) {
  mx.clear();
  uint8_t cBuf[8];
  int pos = 0;

  for (int i = 0; txt[i] != '\0'; i++) {
    int width = mx.getChar(txt[i], sizeof(cBuf), cBuf);
    for (int col = 0; col < width; col++) {
      mx.setColumn(pos++, flipByte(cBuf[col]));  // <<< dibalik
    }
    mx.setColumn(pos++, 0); // spasi antar huruf
  }

  mx.update();
}


void resetGame() {
  for (int y = 0; y < PLAY_H; y++) {
    for (int x = 0; x < PLAY_W; x++) board[y][x] = 0;
  }
  score = 0;
  gameOver = false;

  // tampilkan teks TETRIS tiap reset
  showStaticText("TETRIS");
  delay(2000);

  spawnPiece();
  lastDrop = millis();
}

void setup() {
  Serial.begin(115200);
  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, 5);
  mx.clear();
  pinMode(SW, INPUT_PULLUP);
  randomSeed(analogRead(A5));

  // tampilkan teks TETRIS pertama kali
  showStaticText("TETRIS");
  delay(2000);

  resetGame();
}

void loop() {
  Serial.println(score);
  Serial.println(gameOver);
  if (gameOver) {
    mx.clear();

    // tampilkan score
    String txt = String(score);
    uint8_t cBuf[8];
    int pos = 0;

    for (int i = 0; i < txt.length(); i++) {
      char c = txt[i];
      int width = mx.getChar(c, sizeof(cBuf), cBuf);

      for (int col = 0; col < width; col++) {
        mx.setColumn(pos++, flipByte(cBuf[col]));
      }
      mx.setColumn(pos++, 0);
    }

    mx.update();

    // cek tombol → restart
    if (digitalRead(SW) == LOW) {
      resetGame();
    }
    delay(100);
    return;
  }

  // --- sisanya gameplay tetap ---
  int xVal = analogRead(VRX);
  int yVal = analogRead(VRY);
  bool btn = (digitalRead(SW) == LOW);

  // Gerakan kiri-kanan
  if (xVal < 300) {
    if (canPlace(piece.x - 1, piece.y, piece.type, piece.rot)) piece.x--;
    delay(120);
  } else if (xVal > 700) {
    if (canPlace(piece.x + 1, piece.y, piece.type, piece.rot)) piece.x++;
    delay(120);
  }

  // Rotasi
  if (btn && !btnPressedLast) {
    uint8_t newRot = (piece.rot + 1) % 4;
    if (canPlace(piece.x, piece.y, piece.type, newRot)) piece.rot = newRot;
  }
  btnPressedLast = btn;

  // Soft drop / Hard drop
  if (yVal > 700) {
    if (dropBtnPressTime == 0) dropBtnPressTime = millis();
    unsigned long held = millis() - dropBtnPressTime;
    if (held < 400) {
      if (canPlace(piece.x, piece.y + 1, piece.type, piece.rot)) {
        piece.y++;
      } else {
        lockPiece();
      }
      delay(60);
    } else {
      hardDrop();
      dropBtnPressTime = 0;
      delay(180);
    }
  } else {
    dropBtnPressTime = 0;
  }

  // Gravity otomatis
  unsigned long now = millis();
  if (now - lastDrop > dropInterval) {
    if (canPlace(piece.x, piece.y + 1, piece.type, piece.rot)) {
      piece.y++;
    } else {
      lockPiece();
    }
    lastDrop = now;
  }

  drawBoard();
}
