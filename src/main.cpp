#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cglm/cglm.h>
#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <deque>
#include <memory>
#include <vector>

// ============================================================================
// Action / Command Queue System
// ============================================================================

enum class ActionStatus
{
    Running,
    Finished
};

class Action
{
public:
    virtual ~Action() = default;
    virtual void on_start() { }
    virtual ActionStatus update(float dt) = 0;
};

class ActionQueue
{
public:
    void push(std::unique_ptr<Action> action)
    {
        queue.push_back(std::move(action));
    }

    void update(float dt)
    {
        if (queue.empty())
            return;

        if (!started)
        {
            queue.front()->on_start();
            started = true;
        }

        if (queue.front()->update(dt) == ActionStatus::Finished)
        {
            queue.pop_front();
            started = false;
        }
    }

    bool is_empty() const { return queue.empty(); }
    void clear()
    {
        queue.clear();
        started = false;
    }

private:
    std::deque<std::unique_ptr<Action>> queue;
    bool started = false;
};

// ============================================================================
// 7-Segment Digital Clock Renderer
// ============================================================================

struct Vertex
{
    float x, y;
    float r, g, b, a;
};

class DigitalClock
{
public:
    // ARGB: COLOR_ACTIVE = 0xC000AF70, COLOR_INACTIVE = 0x20007F30
    static constexpr float COLOR_ACTIVE[4] = { 0.0f, 175.0f / 255.0f, 112.0f / 255.0f, 192.0f / 255.0f };
    static constexpr float COLOR_INACTIVE[4] = { 0.0f, 127.0f / 255.0f, 48.0f / 255.0f, 32.0f / 255.0f };

    // Segment bitmasks for digits 0-9
    // Bit 0: lower-right | Bit 1: upper-right | Bit 2: lower-left | Bit 3: upper-left
    // Bit 4: middle      | Bit 5: top         | Bit 6: bottom
    static constexpr uint8_t DIGIT_MASKS[10] = {
        0x6F, 0x03, 0x76, 0x73, 0x1B, 0x79, 0x7D, 0x23, 0x7F, 0x7B
    };

    // Positions of the 6 digits: HH : MM : SS
    static constexpr float DIGIT_POSITIONS[6] = {
        0.0f, 1.3f, 3.0f, 4.3f, 6.0f, 7.3f
    };

    void init()
    {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        // Position: 2 floats
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, x));
        glEnableVertexAttribArray(0);

        // Color (RGBA): 4 floats
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, r));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);
    }

    void destroy()
    {
        if (vao)
            glDeleteVertexArrays(1, &vao);
        if (vbo)
            glDeleteBuffers(1, &vbo);
        vao = 0;
        vbo = 0;
    }

    void update_and_render()
    {
        std::time_t rawTime = std::time(nullptr);
        std::tm *timeInfo = std::localtime(&rawTime);

        int digits[6] = {
            timeInfo->tm_hour / 10,
            timeInfo->tm_hour % 10,
            timeInfo->tm_min / 10,
            timeInfo->tm_min % 10,
            timeInfo->tm_sec / 10,
            timeInfo->tm_sec % 10
        };

        // Blink colon separators every 500 ms (equivalent to SetTimer 500ms)
        bool blink = (SDL_GetTicks() / 500) % 2 == 1;

        meshVertices.clear();

        // Draw 6 digits
        for (int i = 0; i < 6; ++i)
        {
            draw_digit(digits[i], DIGIT_POSITIONS[i]);
        }

        // Draw colon separator pairs at x = 2.65 and x = 5.65
        const float *dotColor = blink ? COLOR_ACTIVE : COLOR_INACTIVE;
        draw_dot(2.65f, 0.7f, 0.2f, dotColor);
        draw_dot(2.65f, 1.3f, 0.2f, dotColor);
        draw_dot(5.65f, 0.7f, 0.2f, dotColor);
        draw_dot(5.65f, 1.3f, 0.2f, dotColor);

        // Upload dynamic mesh and draw
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, meshVertices.size() * sizeof(Vertex), meshVertices.data(), GL_DYNAMIC_DRAW);

        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(meshVertices.size()));
        glBindVertexArray(0);
    }

private:
    GLuint vao = 0;
    GLuint vbo = 0;
    std::vector<Vertex> meshVertices;

    // Center offset to center the clock around (0,0) in OpenGL space
    static constexpr float OFFSET_X = 4.25f;
    static constexpr float OFFSET_Y = 1.10f;

    void add_triangle(float x1, float y1, float x2, float y2, float x3, float y3, const float col[4])
    {
        // Invert Y so that 0 is top and 2 is bottom matching GDI coordinates
        meshVertices.push_back({ (x1 - OFFSET_X), -(y1 - OFFSET_Y), col[0], col[1], col[2], col[3] });
        meshVertices.push_back({ (x2 - OFFSET_X), -(y2 - OFFSET_Y), col[0], col[1], col[2], col[3] });
        meshVertices.push_back({ (x3 - OFFSET_X), -(y3 - OFFSET_Y), col[0], col[1], col[2], col[3] });
    }

    void draw_segment(float x, float y, bool horizontal, const float col[4])
    {
        // Base 6-point hexagon from g_segment_points:
        // (0.1, 0.1), (0.2, 0.0), (0.8, 0.0), (0.9, 0.1), (0.8, 0.2), (0.2, 0.2)
        float pts[6][2] = {
            { 0.1f, 0.1f },
            { 0.2f, 0.0f },
            { 0.8f, 0.0f },
            { 0.9f, 0.1f },
            { 0.8f, 0.2f },
            { 0.2f, 0.2f }
        };

        float worldPts[6][2];
        for (int i = 0; i < 6; ++i)
        {
            if (horizontal)
            {
                worldPts[i][0] = x + 0.1f + pts[i][0];
                worldPts[i][1] = y + pts[i][1];
            }
            else
            {
                // Vertical segment: Rotate 90 degrees clockwise and translate (x + 0.2, y + 0.1)
                worldPts[i][0] = x + 0.2f - pts[i][1];
                worldPts[i][1] = y + 0.1f + pts[i][0];
            }
        }

        // Triangulate 6-sided convex polygon into 4 triangles
        add_triangle(worldPts[0][0], worldPts[0][1], worldPts[1][0], worldPts[1][1], worldPts[2][0], worldPts[2][1], col);
        add_triangle(worldPts[0][0], worldPts[0][1], worldPts[2][0], worldPts[2][1], worldPts[5][0], worldPts[5][1], col);
        add_triangle(worldPts[2][0], worldPts[2][1], worldPts[3][0], worldPts[3][1], worldPts[4][0], worldPts[4][1], col);
        add_triangle(worldPts[2][0], worldPts[2][1], worldPts[4][0], worldPts[4][1], worldPts[5][0], worldPts[5][1], col);
    }

    void draw_digit(int val, float posX)
    {
        val = std::clamp(val, 0, 9);
        uint8_t mask = DIGIT_MASKS[val];

        for (int esi = 0; esi < 7; ++esi)
        {
            bool active = (mask >> esi) & 1;
            const float *col = active ? COLOR_ACTIVE : COLOR_INACTIVE;

            bool isHorizontal = (esi & 4) != 0;
            float x = (esi < 2) ? (posX + 1.0f) : posX;
            float y = (esi == 6) ? 2.0f : ((esi % 2 == 0) ? 1.0f : 0.0f);

            draw_segment(x, y, isHorizontal, col);
        }
    }

    void draw_dot(float x, float y, float size, const float col[4])
    {
        // Approximate circular dot with a 12-sided regular polygon
        constexpr int segments = 12;
        float radius = size * 0.5f;
        float cx = x + radius;
        float cy = y + radius;

        for (int i = 0; i < segments; ++i)
        {
            float a1 = (float)i * (2.0f * 3.14159265f / segments);
            float a2 = (float)(i + 1) * (2.0f * 3.14159265f / segments);

            float x1 = cx + radius * std::cos(a1);
            float y1 = cy + radius * std::sin(a1);
            float x2 = cx + radius * std::cos(a2);
            float y2 = cy + radius * std::sin(a2);

            add_triangle(cx, cy, x1, y1, x2, y2, col);
        }
    }
};

// ============================================================================
// Application & Render State
// ============================================================================

struct AppState
{
    SDL_Window *window = nullptr;
    SDL_GLContext glContext = nullptr;

    GLuint shaderProgram = 0;
    GLint uMVPLoc = -1;

    float rotationAngle = 0.0f;
    float scale = 1.0f;
    float clearColor[3] = { 0.05f, 0.05f, 0.07f }; // Dark background for LED glow

    uint64_t lastTicks = 0;
    ActionQueue actionQueue;
    DigitalClock clock;
};

// ============================================================================
// Concrete Cutscene Actions
// ============================================================================

class RotateAction : public Action
{
public:
    RotateAction(AppState *state, float targetAngleRad, float duration)
        : state(state)
        , target(targetAngleRad)
        , duration(duration)
    {
    }

    void on_start() override
    {
        startAngle = state->rotationAngle;
        elapsed = 0.0f;
    }

    ActionStatus update(float dt) override
    {
        elapsed += dt;
        float t = (duration > 0.0f) ? std::min(1.0f, elapsed / duration) : 1.0f;
        state->rotationAngle = startAngle + (target - startAngle) * t;
        return (t >= 1.0f) ? ActionStatus::Finished : ActionStatus::Running;
    }

private:
    AppState *state;
    float startAngle = 0.0f;
    float target;
    float duration;
    float elapsed = 0.0f;
};

class PulseAction : public Action
{
public:
    PulseAction(AppState *state, float maxScale, float duration)
        : state(state)
        , maxScale(maxScale)
        , duration(duration)
    {
    }

    ActionStatus update(float dt) override
    {
        elapsed += dt;
        float t = std::min(1.0f, elapsed / duration);
        state->scale = 1.0f + (maxScale - 1.0f) * std::sin(t * 3.14159265f);
        return (t >= 1.0f) ? ActionStatus::Finished : ActionStatus::Running;
    }

private:
    AppState *state;
    float maxScale;
    float duration;
    float elapsed = 0.0f;
};

static void setupCutscene(AppState *state)
{
    // Gentle floating pulse & rotation effect
    state->actionQueue.push(std::make_unique<PulseAction>(state, 1.15f, 1.2f));
    state->actionQueue.push(std::make_unique<RotateAction>(state, 0.15f, 0.8f));
    state->actionQueue.push(std::make_unique<RotateAction>(state, -0.15f, 1.6f));
    state->actionQueue.push(std::make_unique<RotateAction>(state, 0.0f, 0.8f));
}

// ============================================================================
// Shaders with Alpha Channel Support
// ============================================================================

const char *vertexShaderSource =
    "#version 330 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "layout (location = 1) in vec4 aColor;\n"
    "uniform mat4 uMVP;\n"
    "out vec4 vColor;\n"
    "void main() {\n"
    "    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);\n"
    "    vColor = aColor;\n"
    "}\0";

const char *fragmentShaderSource =
    "#version 330 core\n"
    "in vec4 vColor;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    FragColor = vColor;\n"
    "}\0";

static GLuint compileShader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    return shader;
}

// ============================================================================
// SDL3 Callbacks
// ============================================================================

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    // Configure SDL3 to invoke SDL_AppIterate at 60 FPS
    if (!SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "60"))
    {
        SDL_Log("Warning: Failed to set callback rate hint: %s", SDL_GetError());
    }

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("Unable to initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    auto *state = new AppState();
    *appstate = state;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    state->window = SDL_CreateWindow(
        "Digital Clock (SDL3 Callbacks + OpenGL + cglm)",
        900, 360,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    if (!state->window)
    {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->glContext = SDL_GL_CreateContext(state->window);
    if (!state->glContext)
    {
        SDL_Log("Failed to create OpenGL context: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_GL_MakeCurrent(state->window, state->glContext);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        SDL_Log("Failed to initialize GLAD");
        return SDL_APP_FAILURE;
    }

    // Enable Alpha Blending for the inactive segment transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    state->shaderProgram = glCreateProgram();
    glAttachShader(state->shaderProgram, vs);
    glAttachShader(state->shaderProgram, fs);
    glLinkProgram(state->shaderProgram);

    glDeleteShader(vs);
    glDeleteShader(fs);

    state->uMVPLoc = glGetUniformLocation(state->shaderProgram, "uMVP");

    // Initialize clock buffers
    state->clock.init();

    state->lastTicks = SDL_GetTicksNS();
    setupCutscene(state);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT)
    {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    auto *state = static_cast<AppState *>(appstate);

    uint64_t currentTicks = SDL_GetTicksNS();
    float dt = static_cast<float>(currentTicks - state->lastTicks) / 1e9f;
    state->lastTicks = currentTicks;

    // Advance Cutscene / Actions
    state->actionQueue.update(dt);
    if (state->actionQueue.is_empty())
    {
        setupCutscene(state);
    }

    // Render Frame
    int w, h;
    SDL_GetWindowSizeInPixels(state->window, &w, &h);
    glViewport(0, 0, w, h);

    glClearColor(state->clearColor[0], state->clearColor[1], state->clearColor[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(state->shaderProgram);

    // Matrix Math using cglm
    float aspect = (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;

    mat4 proj, view, model, vp, mvp;

    // View boundaries: the clock is ~9 units wide and ~2.5 units tall
    float orthoHalfHeight = 2.2f;
    float orthoHalfWidth = orthoHalfHeight * aspect;
    glm_ortho(-orthoHalfWidth, orthoHalfWidth, -orthoHalfHeight, orthoHalfHeight, -1.0f, 1.0f, proj);

    vec3 eye = { 0.0f, 0.0f, 1.0f };
    vec3 center = { 0.0f, 0.0f, 0.0f };
    vec3 up = { 0.0f, 1.0f, 0.0f };
    glm_lookat(eye, center, up, view);

    glm_mat4_identity(model);
    vec3 axisZ = { 0.0f, 0.0f, 1.0f };
    glm_rotate(model, state->rotationAngle, axisZ);
    glm_scale_uni(model, state->scale);

    glm_mat4_mul(proj, view, vp);
    glm_mat4_mul(vp, model, mvp);

    glUniformMatrix4fv(state->uMVPLoc, 1, GL_FALSE, (const GLfloat *)mvp);

    // Render the 7-segment digital clock
    state->clock.update_and_render();

    SDL_GL_SwapWindow(state->window);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    if (appstate)
    {
        auto *state = static_cast<AppState *>(appstate);
        state->clock.destroy();
        if (state->shaderProgram)
            glDeleteProgram(state->shaderProgram);
        if (state->glContext)
            SDL_GL_DestroyContext(state->glContext);
        if (state->window)
            SDL_DestroyWindow(state->window);
        delete state;
    }
    SDL_Quit();
}
