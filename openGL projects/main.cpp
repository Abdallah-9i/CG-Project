// ============================================================
//  ORBIT SHIELD: CS2206 Computer Graphics Project 2026
//  Concept: Defend Earth from incoming asteroids by rotating
//           an orbital cannon around the planet and shooting.
//
//  Controls:
//    Left / Right Arrow  - Rotate the cannon
//    Space               - Fire a bullet
//    + / -               - Zoom in / Zoom out
//    Enter               - Start / Restart the game
//    Q or Esc            - Quit
// ============================================================

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#include <GLUT/glut.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/freeglut.h>
#endif

#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>
using namespace std;

// ============================================================
//  Constants
// ============================================================
const float PI            = 3.14159265f;
const int   WIN_W         = 700;
const int   WIN_H         = 700;

const float EARTH_R       = 0.13f;   // Earth circle radius (world units)
const float ORBIT_R       = 0.25f;   // Cannon orbit radius
const float CANNON_LEN    = 0.07f;   // Cannon barrel length
const float BULLET_SPEED  = 0.025f;  // Bullet units per frame
const float SPAWN_R       = 1.35f;   // Distance at which asteroids spawn
const float BASE_AST_SPD  = 0.00005f; // Slowest asteroid speed

const int   MAX_LIVES     = 3;
const int   SPAWN_DELAY   = 160;     // Frames between asteroid spawns

// ============================================================
//  Game State
// ============================================================
enum Scene { TITLE, PLAYING, GAMEOVER, WIN };

Scene scene         = TITLE;
int   score         = 0;
int   lives         = MAX_LIVES;
int   wave          = 1;
int   killed        = 0;   // asteroids destroyed this wave
int   waveGoal      = 5;   // asteroids needed to finish the wave
int   spawnCount    = 0;   // asteroids spawned this wave
int   spawnTimer    = 0;
int   waveShowTimer = 0;   // frames remaining to show the wave banner
float cannonAngle   = 90.0f; // cannon position in degrees (90 = top)
float zoomLevel     = 1.0f;

// ============================================================
//  Data Structures
// ============================================================
struct Bullet {
    float x, y;   // position
    float dx, dy; // velocity per frame
    bool  alive;
};

struct Asteroid {
    float x, y;        // position
    float dx, dy;      // velocity per frame
    float size;        // scale of the polygon
    float spin;        // current rotation angle (degrees)
    float spinSpeed;   // rotation speed (degrees per frame)
    int   shape;       // silhouette index: 0, 1, or 2
    bool  alive;
};

vector<Bullet>   bullets;
vector<Asteroid> asteroids;

// ============================================================
//  Textures  (procedurally generated - no external files needed)
// ============================================================
GLuint texSpace;  // space background (starfield)
GLuint texEarth;  // Earth surface (blue ocean + green land)

// Starfield: near-black background with scattered white dots
void makeSpaceTexture()
{
    const int W = 256, H = 256;
    static unsigned char data[W * H * 3];

    memset(data, 6, sizeof(data)); // very dark base color

    srand(42); // fixed seed so the starfield never changes
    for (int i = 0; i < 280; i++) {
        int x = rand() % W;
        int y = rand() % H;
        int b = 150 + rand() % 105; // brightness 150-255
        data[(y * W + x) * 3 + 0] = b;
        data[(y * W + x) * 3 + 1] = b;
        data[(y * W + x) * 3 + 2] = b;
    }

    glGenTextures(1, &texSpace);
    glBindTexture(GL_TEXTURE_2D, texSpace);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, W, H, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

// Earth surface: green land patches on a blue ocean
void makeEarthTexture()
{
    const int W = 128, H = 128;
    static unsigned char data[W * H * 3];

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float nx = (float)x / W;
            float ny = (float)y / H;
            // Simple trigonometric noise for a continent-like pattern
            float v = sinf(nx * 9.0f) * cosf(ny * 7.0f)
                    + 0.4f * sinf(nx * 22.0f + ny * 17.0f);
            int idx = (y * W + x) * 3;
            if (v > 0.15f) {                     // land
                data[idx]   = 34  + (int)(v * 40);
                data[idx+1] = 110 + (int)(v * 40);
                data[idx+2] = 34;
            } else {                              // ocean
                data[idx]   = 10;
                data[idx+1] = 60 + (int)((v + 0.5f) * 80);
                data[idx+2] = 160 + (int)((v + 0.5f) * 50);
            }
        }
    }

    glGenTextures(1, &texEarth);
    glBindTexture(GL_TEXTURE_2D, texEarth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, W, H, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

// ============================================================
//  Drawing Helpers
// ============================================================

// Draw a filled polygon or outline circle
void drawCircle(float cx, float cy, float r, int segs, bool filled)
{
    glBegin(filled ? GL_POLYGON : GL_LINE_LOOP);
    for (int i = 0; i < segs; i++) {
        float a = 2.0f * PI * i / segs;
        glVertex2f(cx + r * cosf(a), cy + r * sinf(a));
    }
    glEnd();
}

// Bitmap text rendered at a 2D world position
void drawText(float x, float y, const char* s,
              void* font = GLUT_BITMAP_HELVETICA_18)
{
    glRasterPos2f(x, y);
    for (; *s; s++) glutBitmapCharacter(font, *s);
}

// Stroke (scalable vector) text - useful for large headings
void drawBigText(float x, float y, float scale, const char* s)
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glScalef(scale, scale, 1.0f); // scaling transformation
    for (; *s; s++) glutStrokeCharacter(GLUT_STROKE_ROMAN, *s);
    glPopMatrix();
}

// ============================================================
//  View Setup
// ============================================================

void setupGameView()
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // Use zoomLevel to control the scale/orthographic bounds
    gluOrtho2D(-1.0 * zoomLevel, 1.0 * zoomLevel, -1.0 * zoomLevel, 1.0 * zoomLevel);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

// Fixed UI overlay view
void setupHUDView()
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(-1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

// ============================================================
//  Scene Drawing
// ============================================================

void drawBackground()
{
    // Tile the starfield texture across the full screen quad
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texSpace);
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
        glTexCoord2f(2.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
        glTexCoord2f(2.0f, 2.0f); glVertex2f( 1.0f,  1.0f);
        glTexCoord2f(0.0f, 2.0f); glVertex2f(-1.0f,  1.0f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void drawEarth()
{
    // Draw the Earth as a circle with the procedural texture mapped to it
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texEarth);
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_POLYGON);
    for (int i = 0; i < 64; i++) {
        float a = 2.0f * PI * i / 64.0f;
        glTexCoord2f(0.5f + 0.5f * cosf(a), 0.5f + 0.5f * sinf(a));
        glVertex2f(EARTH_R * cosf(a), EARTH_R * sinf(a));
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);

    // Blue atmosphere ring around the planet
    glColor3f(0.3f, 0.6f, 1.0f);
    glLineWidth(3.0f);
    drawCircle(0.0f, 0.0f, EARTH_R + 0.012f, 64, false);
    glLineWidth(1.0f);
}

void drawOrbitRing()
{
    // Dashed circle showing the cannon's orbit path
    glColor3f(0.45f, 0.5f, 0.9f);
    glLineWidth(1.0f);
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(1, 0xAAAA);
    drawCircle(0.0f, 0.0f, ORBIT_R, 80, false);
    glDisable(GL_LINE_STIPPLE);
}

void drawCannon()
{
    // Compute the cannon's world position from its angle
    float ar = cannonAngle * PI / 180.0f;
    float cx = ORBIT_R * cosf(ar);  // translation (geometric transformation)
    float cy = ORBIT_R * sinf(ar);

    glPushMatrix();
    glTranslatef(cx, cy, 0.0f);               // move to orbit position
    glRotatef(cannonAngle - 90.0f, 0, 0, 1); // rotate barrel to face outward

    // Hexagonal base
    glColor3f(0.55f, 0.60f, 0.70f);
    glBegin(GL_POLYGON);
    for (int i = 0; i < 6; i++) {
        float a = 2.0f * PI * i / 6.0f;
        glVertex2f(0.022f * cosf(a), 0.022f * sinf(a));
    }
    glEnd();

    // Barrel (rectangle pointing outward from Earth)
    glColor3f(0.75f, 0.82f, 0.95f);
    glBegin(GL_QUADS);
        glVertex2f(-0.011f, 0.0f);
        glVertex2f( 0.011f, 0.0f);
        glVertex2f( 0.011f, CANNON_LEN);
        glVertex2f(-0.011f, CANNON_LEN);
    glEnd();

    // Blue glow outline on the barrel
    glColor3f(0.2f, 0.7f, 1.0f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(-0.011f, 0.0f);
        glVertex2f( 0.011f, 0.0f);
        glVertex2f( 0.011f, CANNON_LEN);
        glVertex2f(-0.011f, CANNON_LEN);
    glEnd();
    glLineWidth(1.0f);

    glPopMatrix();
}

// Three asteroid polygon silhouettes (normalized to [-1, 1] range, scaled at draw time)
static const float AST_VERTS[3][8][2] = {
    { {0.9f,0.2f},{0.6f,0.8f},{0.0f,1.0f},{-0.7f,0.7f},
      {-1.0f,0.0f},{-0.6f,-0.8f},{0.1f,-1.0f},{0.8f,-0.5f} }, // 8-sided
    { {0.5f,0.3f},{1.0f,0.0f},{0.4f,-0.5f},{-0.3f,-0.3f},
      {-1.0f,0.1f},{-0.5f,0.5f},{0.0f,0.0f},{0.0f,0.0f} },    // 6-sided
    { {0.0f,1.0f},{0.8f,0.3f},{0.6f,-0.7f},{-0.2f,-1.0f},
      {-0.9f,-0.2f},{-0.5f,0.6f},{0.0f,0.0f},{0.0f,0.0f} }    // 6-sided
};
static const int AST_COUNT[3] = { 8, 6, 6 };

void drawOneAsteroid(const Asteroid& a)
{
    int   n = AST_COUNT[a.shape];
    float s = a.size;

    glPushMatrix();
    glTranslatef(a.x, a.y, 0.0f);
    glRotatef(a.spin, 0, 0, 1); // self-rotation (geometric transformation)

    // Rocky gray fill
    glColor3f(0.62f, 0.58f, 0.52f);
    glBegin(GL_POLYGON);
    for (int i = 0; i < n; i++)
        glVertex2f(AST_VERTS[a.shape][i][0] * s,
                   AST_VERTS[a.shape][i][1] * s);
    glEnd();

    // Lighter inner highlight to give a 3D rock look
    glColor3f(0.78f, 0.74f, 0.68f);
    glBegin(GL_POLYGON);
    for (int i = 0; i < n; i++)
        glVertex2f(AST_VERTS[a.shape][i][0] * s * 0.45f,
                  (AST_VERTS[a.shape][i][1] * s * 0.45f) + s * 0.18f);
    glEnd();

    // Dark outline
    glColor3f(0.25f, 0.22f, 0.20f);
    glLineWidth(1.5f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < n; i++)
        glVertex2f(AST_VERTS[a.shape][i][0] * s,
                   AST_VERTS[a.shape][i][1] * s);
    glEnd();
    glLineWidth(1.0f);

    glPopMatrix();
}

void drawBullets()
{
    for (const auto& b : bullets) {
        if (!b.alive) continue;

        // Yellow plasma head
        glColor3f(1.0f, 0.95f, 0.2f);
        glPointSize(6.0f);
        glBegin(GL_POINTS);
            glVertex2f(b.x, b.y);
        glEnd();

        // Orange trail behind the bullet
        glColor3f(1.0f, 0.45f, 0.0f);
        glPointSize(3.0f);
        glBegin(GL_POINTS);
            glVertex2f(b.x - b.dx * 3, b.y - b.dy * 3);
        glEnd();
    }
    glPointSize(1.0f);
}

void drawHUD()
{
    char buf[64];

    // Score (top-left)
    glColor3f(1.0f, 1.0f, 1.0f);
    sprintf(buf, "Score: %d", score);
    drawText(-0.95f, 0.90f, buf);

    // Wave number
    sprintf(buf, "Wave: %d", wave);
    drawText(-0.95f, 0.81f, buf);

    // Wave progress bar (shows how many asteroids remain to kill)
    float progress = (waveGoal > 0) ? (float)killed / waveGoal : 1.0f;
    if (progress > 1.0f) progress = 1.0f;

    glColor3f(0.25f, 0.25f, 0.25f); // background bar
    glBegin(GL_QUADS);
        glVertex2f(-0.95f, 0.73f); glVertex2f(-0.55f, 0.73f);
        glVertex2f(-0.55f, 0.77f); glVertex2f(-0.95f, 0.77f);
    glEnd();

    glColor3f(0.15f, 0.8f, 0.25f); // green fill
    glBegin(GL_QUADS);
        glVertex2f(-0.95f,                    0.73f);
        glVertex2f(-0.95f + 0.4f * progress,  0.73f);
        glVertex2f(-0.95f + 0.4f * progress,  0.77f);
        glVertex2f(-0.95f,                    0.77f);
    glEnd();

    // Lives shown as red circles (top-right)
    for (int i = 0; i < MAX_LIVES; i++) {
        float cx = 0.72f + i * 0.10f;
        // filled red if life is available, dark if lost
        if (i < lives) glColor3f(1.0f, 0.2f, 0.2f);
        else           glColor3f(0.3f, 0.05f, 0.05f);
        drawCircle(cx, 0.88f, 0.028f, 14, true);
        glColor3f(0.7f, 0.0f, 0.0f);
        glLineWidth(1.5f);
        drawCircle(cx, 0.88f, 0.028f, 14, false);
    }
    glLineWidth(1.0f);

    // Active view zoom label
    glColor3f(0.5f, 0.9f, 0.5f);
    sprintf(buf, "Zoom: %.1fx", 1.0f / zoomLevel);
    drawText(0.51f, 0.78f, buf, GLUT_BITMAP_HELVETICA_12);

    // Controls hint at the bottom
    glColor3f(0.50f, 0.50f, 0.50f);
    drawText(-0.98f, -0.97f,
             "Arrows: Rotate | Space: Shoot | +/-: Zoom | F: Fullscreen | Q: Quit",
             GLUT_BITMAP_HELVETICA_12);

    // Wave announcement banner (fades out after waveShowTimer reaches 0)
    if (waveShowTimer > 0) {
        sprintf(buf, "~ Wave %d ~", wave);
        glColor3f(1.0f, 0.85f, 0.0f);
        drawBigText(-0.32f, 0.05f, 0.0012f, buf);
    }
}

void drawTitleScreen()
{
    drawBackground();

    // Game title
    glColor3f(0.25f, 0.75f, 1.0f);
    drawBigText(-0.55f, 0.55f, 0.0013f, "ORBIT SHIELD");

    glColor3f(0.85f, 0.85f, 0.85f);
    drawText(-0.42f, 0.35f, "Defend Earth from incoming asteroids!");

    // Blinking "Press ENTER" prompt
    static float blink = 0.0f;
    blink += 0.05f;
    if (sinf(blink) > 0.0f) {
        glColor3f(1.0f, 1.0f, 0.25f);
        drawText(-0.22f, -0.30f, "Press ENTER to Start");
    }

    // Controls list
    glColor3f(0.5f, 0.85f, 0.5f);
    drawText(-0.33f, -0.45f, "Left / Right Arrow  -  Rotate Cannon");
    drawText(-0.33f, -0.55f, "Space               -  Fire");
    drawText(-0.33f, -0.65f, "+ / -               -  Zoom In / Out");
    drawText(-0.33f, -0.75f, "F                   -  Fullscreen");
    drawText(-0.33f, -0.85f, "Q or Esc            -  Quit");

    // Preview of the game objects
    drawEarth();
    drawOrbitRing();
    drawCannon();
}

void drawGameOverScreen()
{
    drawBackground();

    glColor3f(1.0f, 0.15f, 0.15f);
    drawBigText(-0.62f, 0.30f, 0.0013f, "GAME OVER");

    char buf[64];
    glColor3f(1.0f, 1.0f, 1.0f);
    sprintf(buf, "Final Score: %d", score);
    drawText(-0.18f, 0.02f, buf);

    sprintf(buf, "Waves Survived: %d", wave - 1);
    drawText(-0.22f, -0.10f, buf);

    glColor3f(1.0f, 1.0f, 0.3f);
    drawText(-0.28f, -0.35f, "Press ENTER to Play Again");

    glColor3f(0.65f, 0.65f, 0.65f);
    drawText(-0.13f, -0.50f, "Press Q to Quit");
}

void drawWinScreen()
{
    drawBackground();

    glColor3f(0.2f, 1.0f, 0.2f);
    drawBigText(-0.42f, 0.30f, 0.0013f, "YOU WIN!");

    char buf[64];
    glColor3f(1.0f, 1.0f, 1.0f);
    sprintf(buf, "Final Score: %d", score);
    drawText(-0.18f, 0.02f, buf);

    sprintf(buf, "Waves Cleared: %d", wave - 1);
    drawText(-0.22f, -0.10f, buf);

    glColor3f(1.0f, 1.0f, 0.3f);
    drawText(-0.28f, -0.35f, "Press ENTER to Play Again");

    glColor3f(0.65f, 0.65f, 0.65f);
    drawText(-0.13f, -0.50f, "Press Q to Quit");
}


// ============================================================
//  Game Logic
// ============================================================

float dist2D(float x1, float y1, float x2, float y2)
{
    float dx = x2 - x1, dy = y2 - y1;
    return sqrtf(dx * dx + dy * dy);
}

void spawnAsteroid()
{
    // Pick a random point on the spawn circle
    float angle = 2.0f * PI * (rand() / (float)RAND_MAX);
    float sx    = SPAWN_R * cosf(angle);
    float sy    = SPAWN_R * sinf(angle);

    // Direction toward Earth center with a small random wobble
    float dx = -sx + 0.2f * (2.0f * rand() / RAND_MAX - 1.0f);
    float dy = -sy + 0.2f * (2.0f * rand() / RAND_MAX - 1.0f);
    float len = sqrtf(dx * dx + dy * dy);

    float speed = BASE_AST_SPD + wave * 0.00015f
                + 0.0004f * (rand() / (float)RAND_MAX);

    Asteroid a;
    a.x         = sx;
    a.y         = sy;
    a.dx        = (dx / len) * speed;
    a.dy        = (dy / len) * speed;
    a.size      = 0.05f + 0.04f * (rand() / (float)RAND_MAX);
    a.spin      = 0.0f;
    a.spinSpeed = (1.5f + 3.0f * (rand() / (float)RAND_MAX))
                  * (rand() % 2 ? 1.0f : -1.0f);
    a.shape     = rand() % 3;
    a.alive     = true;

    asteroids.push_back(a);
    spawnCount++;
}

void shoot()
{
    float ar  = cannonAngle * PI / 180.0f;
    float cx  = ORBIT_R * cosf(ar);
    float cy  = ORBIT_R * sinf(ar);
    float len = sqrtf(cx * cx + cy * cy);

    Bullet b;
    b.dx    = (cx / len) * BULLET_SPEED;
    b.dy    = (cy / len) * BULLET_SPEED;
    b.x     = cx + b.dx * 5; // spawn slightly ahead of cannon tip
    b.y     = cy + b.dy * 5;
    b.alive = true;
    bullets.push_back(b);
}

void updateBullets()
{
    for (auto& b : bullets) {
        if (!b.alive) continue;
        b.x += b.dx;
        b.y += b.dy;
        // Deactivate when it leaves the visible area
        if (fabsf(b.x) > 1.6f || fabsf(b.y) > 1.6f) b.alive = false;
    }
}

void updateAsteroids()
{
    for (auto& a : asteroids) {
        if (!a.alive) continue;
        a.x    += a.dx;              // translation
        a.y    += a.dy;
        a.spin += a.spinSpeed;       // self-rotation (geometric transformation)
    }
}

void checkCollisions()
{
    // Bullet hits asteroid
    for (auto& b : bullets) {
        if (!b.alive) continue;
        for (auto& a : asteroids) {
            if (!a.alive) continue;
            if (dist2D(b.x, b.y, a.x, a.y) < a.size * 1.1f) {
                b.alive = false;
                a.alive = false;
                score  += 10 * wave; // higher waves give more points
                killed++;
                break;
            }
        }
    }

    // Asteroid reaches Earth
    for (auto& a : asteroids) {
        if (!a.alive) continue;
        if (dist2D(a.x, a.y, 0.0f, 0.0f) < EARTH_R + a.size * 0.6f) {
            a.alive = false;
            lives--;
            if (lives <= 0) scene = GAMEOVER;
        }
    }
}

void checkWaveComplete()
{
    // Wave ends when all asteroids for this wave have been spawned
    // AND every one of them is gone (shot down OR hit Earth).
    // Using spawnCount instead of killed so that Earth-impact asteroids
    // don't stall the wave forever.
    if (spawnCount < waveGoal) return;

    int remaining = 0;
    for (const auto& a : asteroids) if (a.alive) remaining++;
    if (remaining > 0) return;

    // All done — advance to the next wave
    wave++;

    if (wave > 3) {
        scene = WIN;
        return; 
    }

    killed        = 0;
    spawnCount    = 0;
    spawnTimer    = 0;
    waveGoal      = 2 + wave * 1;
    waveShowTimer = 180;
    asteroids.clear();
    bullets.clear();
}

void startGame()
{
    score         = 0;
    lives         = MAX_LIVES;
    wave          = 1;
    killed        = 0;
    spawnCount    = 0;
    spawnTimer    = 0;
    waveGoal      = 5;   // wave 1: spawn 5 asteroids to complete it
    waveShowTimer = 180;
    cannonAngle   = 90.0f;
    zoomLevel     = 1.0f;
    asteroids.clear();
    bullets.clear();
    scene = PLAYING;
}

// ============================================================
//  GLUT Callbacks
// ============================================================

void display()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();

    if (scene == TITLE) {
        setupHUDView();
        drawTitleScreen();
    }
    else if (scene == GAMEOVER) {
        setupHUDView();
        drawGameOverScreen();
    }
    else if (scene == WIN) {
        setupHUDView();
        drawWinScreen();
    }
    else { // PLAYING
        glDisable(GL_DEPTH_TEST);
        setupGameView();

        drawBackground();
        drawOrbitRing();
        drawEarth();
        drawCannon();
        for (const auto& a : asteroids) drawOneAsteroid(a);
        drawBullets();

        // HUD is always drawn in 2D regardless of zoom level
        glDisable(GL_DEPTH_TEST);
        setupHUDView();
        drawHUD();
    }

    glFlush();
    glutSwapBuffers();
}

void idle()
{
    if (scene != PLAYING) {
        glutPostRedisplay();
        return;
    }

    // Spawn a new asteroid on a timer (only if the wave quota isn't met yet)
    int activeCount = 0;
    for (const auto& a : asteroids) if (a.alive) activeCount++;

    spawnTimer++;
    if (spawnTimer >= SPAWN_DELAY && spawnCount < waveGoal && activeCount < 1) {
        spawnAsteroid();
        spawnTimer = 0;
    }

    updateBullets();
    updateAsteroids();
    checkCollisions();
    checkWaveComplete();

    if (waveShowTimer > 0) waveShowTimer--;

    glutPostRedisplay();
}

void keyboard(unsigned char key, int x, int y)
{
    switch (key) {
        case ' ':            // fire
            if (scene == PLAYING) shoot();
            break;
        case '+': case '=':  // zoom in
            if (scene == PLAYING && zoomLevel > 0.5f) zoomLevel -= 0.1f;
            break;
        case '-': case '_':  // zoom out
            if (scene == PLAYING && zoomLevel < 2.0f) zoomLevel += 0.1f;
            break;
        case 'f': case 'F':  // toggle fullscreen
            glutFullScreenToggle();
            break;
        case 13:             // Enter - start or restart
            if (scene == TITLE || scene == GAMEOVER) startGame();
            break;
        case 'q': case 'Q':
        case 27:             // Escape - quit
            exit(0);
        default: break;
    }
}

void special(int key, int x, int y)
{
    if (scene != PLAYING) return;
    const float STEP = 15.0f; // degrees per key press
    switch (key) {
        case GLUT_KEY_LEFT:  cannonAngle += STEP; break; // counter-clockwise
        case GLUT_KEY_RIGHT: cannonAngle -= STEP; break; // clockwise
        default: break;
    }
}

void mouse(int button, int state, int x, int y)
{
    if (state != GLUT_UP) return;
    if (button == GLUT_LEFT_BUTTON && scene == PLAYING) shoot();
    if (button == GLUT_RIGHT_BUTTON && (scene == TITLE || scene == GAMEOVER))
        startGame();
}

void reshape(int w, int h)
{
    if (h == 0) h = 1;
    // Maintain a square viewport centered in the window so the game
    // never stretches on widescreen or fullscreen displays.
    int size = (w < h) ? w : h;
    int xOff = (w - size) / 2;
    int yOff = (h - size) / 2;
    glViewport(xOff, yOff, size, size);
}

void init()
{
    glClearColor(0.0f, 0.0f, 0.05f, 1.0f);
    srand((unsigned)time(nullptr));
    makeSpaceTexture();
    makeEarthTexture();
}

// ============================================================
//  Main
// ============================================================
int main(int argc, char** argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
    glutInitWindowSize(WIN_W, WIN_H);
    glutInitWindowPosition(100, 50);
    glutCreateWindow("Orbit Shield - CS2206 CG Project 2026");

    init();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutIdleFunc(idle);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(special);
    glutMouseFunc(mouse);

    glutMainLoop();
    return 0;
}
