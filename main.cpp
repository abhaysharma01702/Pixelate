#include <arduino.h>
#include <FastLED.h>

#define LED_PIN     14
#define COLOR_ORDER GRB
#define CHIPSET     WS2812B
#define kMatrixWidth   19
#define kMatrixHeight  19
#define NUM_LEDS (kMatrixWidth * kMatrixHeight)

// Set to 1 for Wokwi, 0 for real hardware
#define IS_SIMULATOR 1 

CRGB leds[NUM_LEDS];

// Simple pseudo-random number generator
uint32_t rngSeed = 12345;

uint16_t myRandom(uint16_t maxVal) {
  rngSeed = (rngSeed * 1103515245 + 12345) & 0x7FFFFFFF;
  return (rngSeed % maxVal);
}

uint16_t myRandom(uint16_t minVal, uint16_t maxVal) {
  return minVal + myRandom(maxVal - minVal);
}

// ============= SNAKE GAME ANIMATION =============
#define MAX_SNAKE_LENGTH 200

// Snake body positions
struct Point {
  int8_t x;
  int8_t y;
};

Point snake[MAX_SNAKE_LENGTH];
uint8_t snakeLength = 3;
Point food;
Point direction = {1, 0}; // Start moving right

// Game state
enum GameState { PLAYING, EATING, GAME_OVER, RESTARTING };
GameState gameState = PLAYING;
uint8_t animTimer = 0;
uint8_t moveDelay = 8; // Frames between moves
uint8_t frameCounter = 0;

// Colors
CRGB snakeHeadColor = CRGB(255, 215, 0);   // Gold/yellow head
CRGB snakeBodyColor = CRGB(0, 180, 50);    // Green body
CRGB foodColor = CRGB(255, 50, 0);         // Bright red food
CRGB bgColor = CRGB(0, 0, 0);              // Black background
CRGB wallColor = CRGB(30, 30, 100);        // Dim blue walls

// Firework particles
#define MAX_PARTICLES 50
struct Particle {
  float x, y;
  float vx, vy;
  CRGB color;
  uint8_t life;
};
Particle particles[MAX_PARTICLES];

// ==========================================================
// MAPPING
// ==========================================================
uint16_t XY(uint8_t x, uint8_t y) {
  if (x >= kMatrixWidth || y >= kMatrixHeight) return NUM_LEDS;
  if (IS_SIMULATOR) return (y * kMatrixWidth) + x;
  if (y % 2 == 0) return (y * kMatrixWidth) + x;
  return (y * kMatrixWidth) + (18 - x);
}

// ==========================================================
// SNAKE GAME LOGIC
// ==========================================================

// Forward declaration
void spawnFood();

void initSnake() {
  snakeLength = 3;
  snake[0] = {9, 9};   // Head in center
  snake[1] = {8, 9};   // Body segment 1
  snake[2] = {7, 9};   // Body segment 2
  direction = {1, 0};  // Moving right
  spawnFood();
  
  // Initialize particles
  for (uint8_t i = 0; i < MAX_PARTICLES; i++) {
    particles[i].life = 0;
  }
}

void spawnFood() {
  // Find random empty position
  bool validPosition = false;
  while (!validPosition) {
    food.x = myRandom(1, kMatrixWidth - 1);
    food.y = myRandom(1, kMatrixHeight - 1);
    
    // Check if food is on snake
    validPosition = true;
    for (uint8_t i = 0; i < snakeLength; i++) {
      if (snake[i].x == food.x && snake[i].y == food.y) {
        validPosition = false;
        break;
      }
    }
  }
}

bool isOccupied(int8_t x, int8_t y) {
  // Check walls
  if (x <= 0 || x >= kMatrixWidth - 1 || y <= 0 || y >= kMatrixHeight - 1) {
    return true;
  }
  
  // Check snake body (skip last segment as it will move)
  for (uint8_t i = 0; i < snakeLength - 1; i++) {
    if (snake[i].x == x && snake[i].y == y) {
      return true;
    }
  }
  
  return false;
}

// Count free spaces around a position (simple safety check)
uint8_t countFreeSpaces(int8_t x, int8_t y) {
  uint8_t count = 0;
  if (!isOccupied(x + 1, y)) count++;
  if (!isOccupied(x - 1, y)) count++;
  if (!isOccupied(x, y + 1)) count++;
  if (!isOccupied(x, y - 1)) count++;
  return count;
}

void smartDirection() {
  // Calculate direction to food
  int8_t dx = food.x - snake[0].x;
  int8_t dy = food.y - snake[0].y;
  
  // All possible moves
  Point moves[4] = {
    {1, 0},   // Right
    {-1, 0},  // Left
    {0, 1},   // Down
    {0, -1}   // Up
  };
  
  int8_t scores[4] = {-100, -100, -100, -100};
  
  // Score each possible move
  for (uint8_t i = 0; i < 4; i++) {
    Point nextPos = {
      (int8_t)(snake[0].x + moves[i].x),
      (int8_t)(snake[0].y + moves[i].y)
    };
    
    // Don't go backward into neck
    if (snakeLength > 1 && nextPos.x == snake[1].x && nextPos.y == snake[1].y) {
      continue;
    }
    
    // Check if move is valid
    if (isOccupied(nextPos.x, nextPos.y)) {
      continue;
    }
    
    scores[i] = 0;
    
    // Bonus for moving toward food
    int8_t newDx = food.x - nextPos.x;
    int8_t newDy = food.y - nextPos.y;
    int8_t oldDist = abs(dx) + abs(dy);
    int8_t newDist = abs(newDx) + abs(newDy);
    
    if (newDist < oldDist) {
      scores[i] += 30; // Good - getting closer to food
    } else {
      scores[i] -= 10; // Bad - getting farther
    }
    
    // Bonus for having escape routes (avoid traps)
    uint8_t freeSpaces = countFreeSpaces(nextPos.x, nextPos.y);
    scores[i] += freeSpaces * 8;
    
    // Bonus for staying in center area (avoid corners)
    int8_t centerX = kMatrixWidth / 2;
    int8_t centerY = kMatrixHeight / 2;
    int8_t distFromCenter = abs(nextPos.x - centerX) + abs(nextPos.y - centerY);
    scores[i] -= distFromCenter / 2;
    
    // Small random factor to avoid getting stuck in patterns
    scores[i] += myRandom(5);
  }
  
  // Find best move
  int8_t bestScore = -100;
  int8_t bestMove = -1;
  
  for (uint8_t i = 0; i < 4; i++) {
    if (scores[i] > bestScore) {
      bestScore = scores[i];
      bestMove = i;
    }
  }
  
  // Apply best move
  if (bestMove >= 0) {
    direction = moves[bestMove];
  }
}

void moveSnake() {
  // Calculate new head position
  Point newHead = {
    (int8_t)(snake[0].x + direction.x),
    (int8_t)(snake[0].y + direction.y)
  };
  
  // Check for collision (final check)
  if (isOccupied(newHead.x, newHead.y)) {
    // Last resort - try any valid direction
    Point emergency[4] = {{1,0}, {-1,0}, {0,1}, {0,-1}};
    bool foundEscape = false;
    
    for (uint8_t i = 0; i < 4; i++) {
      Point test = {
        (int8_t)(snake[0].x + emergency[i].x),
        (int8_t)(snake[0].y + emergency[i].y)
      };
      if (!isOccupied(test.x, test.y)) {
        newHead = test;
        direction = emergency[i];
        foundEscape = true;
        break;
      }
    }
    
    if (!foundEscape) {
      gameState = GAME_OVER;
      animTimer = 0;
      return;
    }
  }
  
  // Check if eating food
  bool ateFood = (newHead.x == food.x && newHead.y == food.y);
  
  if (ateFood) {
    gameState = EATING;
    animTimer = 0;
  }
  
  // Move snake body
  if (!ateFood) {
    // Shift all segments back
    for (int i = snakeLength - 1; i > 0; i--) {
      snake[i] = snake[i - 1];
    }
  } else {
    // Growing - shift but don't remove tail
    for (int i = snakeLength; i > 0; i--) {
      snake[i] = snake[i - 1];
    }
    snakeLength++;
    if (snakeLength >= MAX_SNAKE_LENGTH) snakeLength = MAX_SNAKE_LENGTH - 1;
    
    // Win condition - game ends at 15 apples
    if (snakeLength >= 18) {
      gameState = GAME_OVER;
      animTimer = 0;
      return;
    }
  }
  
  // Add new head
  snake[0] = newHead;
  
  if (ateFood) {
    spawnFood();
  }
}

void drawGame() {
  // Clear background
  fill_solid(leds, NUM_LEDS, bgColor);
  
  // Draw walls (border)
  for (uint8_t x = 0; x < kMatrixWidth; x++) {
    leds[XY(x, 0)] = wallColor;
    leds[XY(x, kMatrixHeight - 1)] = wallColor;
  }
  for (uint8_t y = 0; y < kMatrixHeight; y++) {
    leds[XY(0, y)] = wallColor;
    leds[XY(kMatrixWidth - 1, y)] = wallColor;
  }
  
  // Draw food with pulse effect
  uint8_t foodBright = beatsin8(30, 100, 255);
  CRGB pulsedFood = foodColor;
  pulsedFood.nscale8(foodBright);
  leds[XY(food.x, food.y)] = pulsedFood;
  
  // Draw snake body
  for (uint8_t i = 1; i < snakeLength; i++) {
    // Gradient from head to tail
    uint8_t brightness = map(i, 1, snakeLength, 255, 100);
    CRGB segmentColor = snakeBodyColor;
    segmentColor.nscale8(brightness);
    leds[XY(snake[i].x, snake[i].y)] = segmentColor;
  }
  
  // Draw head (brightest)
  leds[XY(snake[0].x, snake[0].y)] = snakeHeadColor;
}

void drawEatingEffect() {
  drawGame();
  // Flash effect around food location
  for (int8_t dx = -1; dx <= 1; dx++) {
    for (int8_t dy = -1; dy <= 1; dy++) {
      int8_t x = snake[0].x + dx;
      int8_t y = snake[0].y + dy;
      if (x >= 0 && x < kMatrixWidth && y >= 0 && y < kMatrixHeight) {
        leds[XY(x, y)] = CRGB(255, 200, 0); // Yellow flash
      }
    }
  }
}

void drawGameOver() {
  fill_solid(leds, NUM_LEDS, bgColor);
  
  // Launch fireworks
  if (animTimer == 0) {
    // Create firework particles from center
    for (uint8_t i = 0; i < MAX_PARTICLES; i++) {
      particles[i].x = kMatrixWidth / 2.0;
      particles[i].y = kMatrixHeight / 2.0;
      
      // Random velocity in all directions
      float angle = (i * 360.0 / MAX_PARTICLES) * 0.0174533; // Convert to radians
      float speed = 0.3 + (myRandom(100) / 100.0) * 0.4;
      particles[i].vx = cos(angle) * speed;
      particles[i].vy = sin(angle) * speed;
      
      // Rainbow colors
      if (i % 5 == 0) particles[i].color = CRGB(255, 0, 0);      // Red
      else if (i % 5 == 1) particles[i].color = CRGB(255, 165, 0); // Orange
      else if (i % 5 == 2) particles[i].color = CRGB(255, 255, 0); // Yellow
      else if (i % 5 == 3) particles[i].color = CRGB(0, 255, 0);   // Green
      else particles[i].color = CRGB(0, 100, 255);                 // Blue
      
      particles[i].life = 255;
    }
  }
  
  // Update and draw particles
  for (uint8_t i = 0; i < MAX_PARTICLES; i++) {
    if (particles[i].life > 0) {
      // Update position
      particles[i].x += particles[i].vx;
      particles[i].y += particles[i].vy;
      
      // Gravity effect
      particles[i].vy += 0.05;
      
      // Fade out
      particles[i].life -= 3;
      
      // Draw particle
      int8_t px = (int8_t)particles[i].x;
      int8_t py = (int8_t)particles[i].y;
      
      if (px >= 0 && px < kMatrixWidth && py >= 0 && py < kMatrixHeight) {
        CRGB color = particles[i].color;
        color.nscale8(particles[i].life);
        leds[XY(px, py)] = color;
        
        // Sparkle trail
        if (myRandom(3) == 0) {
          int8_t tx = px + myRandom(3) - 1;
          int8_t ty = py + myRandom(3) - 1;
          if (tx >= 0 && tx < kMatrixWidth && ty >= 0 && ty < kMatrixHeight) {
            CRGB trail = color;
            trail.nscale8(particles[i].life / 2);
            leds[XY(tx, ty)] = trail;
          }
        }
      }
    }
  }
  
  // Secondary explosion at halfway point
  if (animTimer == 15) {
    for (uint8_t i = 0; i < MAX_PARTICLES / 2; i++) {
      particles[i].x = kMatrixWidth / 2.0;
      particles[i].y = kMatrixHeight / 2.0;
      
      float angle = myRandom(360) * 0.0174533;
      float speed = 0.2 + (myRandom(100) / 100.0) * 0.3;
      particles[i].vx = cos(angle) * speed;
      particles[i].vy = sin(angle) * speed;
      
      particles[i].color = CRGB(255, 255, 255); // White
      particles[i].life = 200;
    }
  }
}

// ==========================================================
// MAIN LOOP
// ==========================================================
void setup() {
  delay(1000);
  FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(255);
  rngSeed = millis(); // Seed with current time for variety
  initSnake();
}

void loop() {
  frameCounter++;
  
  // --- GAME STATE MACHINE ---
  switch (gameState) {
    case PLAYING:
      // Update snake AI direction every move
      if (frameCounter >= moveDelay) {
        smartDirection();
        frameCounter = 0;
        moveSnake();
      }
      drawGame();
      break;
      
    case EATING:
      // Show eating animation for a few frames
      drawEatingEffect();
      animTimer++;
      if (animTimer > 3) {
        gameState = PLAYING;
        animTimer = 0;
        // Speed up slightly as snake grows
        if (moveDelay > 5 && snakeLength % 5 == 0) {
          moveDelay--;
        }
      }
      break;
      
    case GAME_OVER:
      // Fireworks animation
      drawGameOver();
      animTimer++;
      if (animTimer > 60) {
        gameState = RESTARTING;
        animTimer = 0;
      }
      break;
      
    case RESTARTING:
      // Clear screen with fade effect
      fill_solid(leds, NUM_LEDS, CRGB(0, 0, animTimer * 2));
      animTimer++;
      if (animTimer > 20) {
        // Reset game
        initSnake();
        gameState = PLAYING;
        animTimer = 0;
        frameCounter = 0;
        moveDelay = 8;
      }
      break;
  }
  
  FastLED.show();
  FastLED.delay(30); // ~33 fps
}