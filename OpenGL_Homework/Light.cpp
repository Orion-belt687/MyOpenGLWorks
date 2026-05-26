#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cassert>
#include <iostream>
#include <vector>
#include <cmath>

using namespace std;

// 方向键	旋转摄像机环绕场景
// Q/E	摄像机缩放
// W/A/S/D	移动光源
// 1/2	ambient 强度 -/+
// 3/4	diffuse 强度 -/+
// 5/6	specular 强度 -/+
// 7/8	shininess 光泽度 -/+
// F	切换 Blinn-Phong / Phong
// R	切换光源自动旋转
// ESC	关闭窗口

const unsigned int SCR_WIDTH = 1600;
const unsigned int SCR_HEIGHT = 1000;
const float PI = 3.14159265359f;

// ── lighting shader ────────────────────────────────────────────
const char* lightingVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

out vec3 vWorldPos;
out vec3 vNormal;

uniform mat4 uModel;
uniform mat4 uMVP;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* lightingFragSrc = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;

out vec4 FragColor;

uniform vec3 uLightPos;
uniform vec3 uLightColor;
uniform vec3 uViewPos;
uniform vec3 uObjectColor;
uniform float uAmbientStrength;
uniform float uDiffuseStrength;
uniform float uSpecularStrength;
uniform float uShininess;
uniform bool uBlinnPhong;

void main() {
    vec3 ambient = uAmbientStrength * uLightColor;

    vec3 norm = normalize(vNormal);
    vec3 lightDir = normalize(uLightPos - vWorldPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = uDiffuseStrength * diff * uLightColor;

    vec3 viewDir = normalize(uViewPos - vWorldPos);
    float spec = 0.0;
    if (diff > 0.0) {
        if (uBlinnPhong) {
            vec3 halfwayDir = normalize(lightDir + viewDir);
            spec = pow(max(dot(norm, halfwayDir), 0.0), uShininess);
        } else {
            vec3 reflectDir = reflect(-lightDir, norm);
            spec = pow(max(dot(viewDir, reflectDir), 0.0), uShininess);
        }
    }
    vec3 specular = uSpecularStrength * spec * uLightColor;

    vec3 result = (ambient + diffuse + specular) * uObjectColor;
    FragColor = vec4(result, 1.0);
}
)";

// ── simple unlit shader (for light indicator) ──────────────────
const char* simpleVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* simpleFragSrc = R"(
#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main() {
    FragColor = vec4(uColor, 1.0);
}
)";

// ── error check ─────────────────────────────────────────────────
void checkError() {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        switch (err) {
            case GL_INVALID_ENUM:      cout << "GL_INVALID_ENUM" << endl;      break;
            case GL_INVALID_VALUE:     cout << "GL_INVALID_VALUE" << endl;     break;
            case GL_INVALID_OPERATION: cout << "GL_INVALID_OPERATION" << endl; break;
            case GL_OUT_OF_MEMORY:     cout << "GL_OUT_OF_MEMORY" << endl;     break;
            default:                   cout << "unknown error: " << err << endl;
        }
        assert(false);
    }
}

// ── helpers ─────────────────────────────────────────────────────
unsigned int compileShader(GLenum type, const char* src) {
    unsigned int s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, NULL, log);
        cout << "Shader compile error:\n" << log << endl;
    }
    return s;
}

unsigned int linkProgram(unsigned int vs, unsigned int fs) {
    unsigned int p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    int ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, 512, NULL, log);
        cout << "Program link error:\n" << log << endl;
    }
    return p;
}

// ── sphere generation ───────────────────────────────────────────
struct Mesh {
    unsigned int VAO = 0;
    unsigned int VBO = 0;
    unsigned int EBO = 0;
    int indexCount = 0;
};

Mesh generateSphere(float radius, int sectors, int stacks) {
    vector<float> verts;
    vector<unsigned int> indices;

    for (int i = 0; i <= stacks; i++) {
        float phi = PI * i / stacks;
        float y = cosf(phi);
        float r = sinf(phi);
        for (int j = 0; j <= sectors; j++) {
            float theta = 2.0f * PI * j / sectors;
            float x = r * cosf(theta);
            float z = r * sinf(theta);
            // position
            verts.push_back(radius * x);
            verts.push_back(radius * y);
            verts.push_back(radius * z);
            // normal (same as position for unit sphere)
            verts.push_back(x);
            verts.push_back(y);
            verts.push_back(z);
        }
    }

    for (int i = 0; i < stacks; i++) {
        for (int j = 0; j < sectors; j++) {
            unsigned int k1 = i * (sectors + 1) + j;
            unsigned int k2 = k1 + 1;
            unsigned int k3 = (i + 1) * (sectors + 1) + j;
            unsigned int k4 = k3 + 1;
            indices.push_back(k1); indices.push_back(k2); indices.push_back(k3);
            indices.push_back(k2); indices.push_back(k4); indices.push_back(k3);
        }
    }

    Mesh m;
    m.indexCount = (int)indices.size();
    glGenVertexArrays(1, &m.VAO);
    glGenBuffers(1, &m.VBO);
    glGenBuffers(1, &m.EBO);
    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    return m;
}

// ── floor plane ─────────────────────────────────────────────────
Mesh generateFloor() {
    float verts[] = {
        -5.0f, -1.5f, -5.0f,  0.0f, 1.0f, 0.0f,
         5.0f, -1.5f, -5.0f,  0.0f, 1.0f, 0.0f,
         5.0f, -1.5f,  5.0f,  0.0f, 1.0f, 0.0f,
        -5.0f, -1.5f,  5.0f,  0.0f, 1.0f, 0.0f,
    };
    unsigned int idx[] = { 0, 1, 2, 0, 2, 3 };

    Mesh m;
    m.indexCount = 6;
    glGenVertexArrays(1, &m.VAO);
    glGenBuffers(1, &m.VBO);
    glGenBuffers(1, &m.EBO);
    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    return m;
}

// ── small cube (for light indicator) ────────────────────────────
Mesh generateSmallCube() {
    float verts[] = {
        -0.1f, -0.1f, -0.1f,  0.1f, -0.1f, -0.1f,  0.1f,  0.1f, -0.1f, -0.1f,  0.1f, -0.1f,
        -0.1f, -0.1f,  0.1f,  0.1f, -0.1f,  0.1f,  0.1f,  0.1f,  0.1f, -0.1f,  0.1f,  0.1f,
    };
    unsigned int idx[] = {
        0,1,2, 2,3,0,  4,5,6, 6,7,4,  0,4,7, 7,3,0,
        1,5,6, 6,2,1,  0,1,5, 5,4,0,  3,2,6, 6,7,3,
    };

    Mesh m;
    m.indexCount = 36;
    glGenVertexArrays(1, &m.VAO);
    glGenBuffers(1, &m.VBO);
    glGenBuffers(1, &m.EBO);
    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    return m;
}

// ── main ────────────────────────────────────────────────────────
int main() {
    cout << "=== Lighting Model Demo ===" << endl;
    cout << "Blinn-Phong / Phong lighting on a sphere + floor" << endl;
    cout << endl;
    cout << "Controls:" << endl;
    cout << "  Arrow keys : orbit camera" << endl;
    cout << "  Q / E      : zoom in / out" << endl;
    cout << "  W A S D    : move light (Y+, X-, Y-, X+)" << endl;
    cout << "  1 / 2      : ambient  - / +" << endl;
    cout << "  3 / 4      : diffuse  - / +" << endl;
    cout << "  5 / 6      : specular - / +" << endl;
    cout << "  7 / 8      : shininess - / +" << endl;
    cout << "  F          : toggle Phong / Blinn-Phong" << endl;
    cout << "  R          : toggle light auto-rotate" << endl;
    cout << "  ESC        : close window" << endl;

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Lighting Demo", NULL, NULL);
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        cout << "Failed to initialize GLAD" << endl;
        return -1;
    }

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
    glEnable(GL_DEPTH_TEST);

    // compile shaders
    unsigned int litVS = compileShader(GL_VERTEX_SHADER, lightingVertSrc);
    unsigned int litFS = compileShader(GL_FRAGMENT_SHADER, lightingFragSrc);
    unsigned int litProg = linkProgram(litVS, litFS);

    unsigned int simpleVS = compileShader(GL_VERTEX_SHADER, simpleVertSrc);
    unsigned int simpleFS = compileShader(GL_FRAGMENT_SHADER, simpleFragSrc);
    unsigned int simpleProg = linkProgram(simpleVS, simpleFS);

    glDeleteShader(litVS); glDeleteShader(litFS);
    glDeleteShader(simpleVS); glDeleteShader(simpleFS);

    // geometry
    Mesh sphere = generateSphere(1.0f, 40, 20);
    Mesh floor  = generateFloor();
    Mesh cube   = generateSmallCube();

    // lighting params
    float ambientStr   = 0.15f;
    float diffuseStr   = 0.7f;
    float specularStr  = 0.5f;
    float shininess    = 32.0f;
    bool blinnPhong    = true;
    bool autoRotate    = true;

    // light params
    float lightAngle = 0.0f;
    float lightHeight = 2.0f;
    float lightRadius = 2.5f;

    // camera params
    float camAngle = -0.8f;
    float camElevation = 0.5f;
    float camDist = 5.5f;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // ── keyboard ────────────────────────────────────────
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        // camera orbit
        if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) camAngle -= 0.02f;
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) camAngle += 0.02f;
        if (glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) camElevation += 0.02f;
        if (glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) camElevation -= 0.02f;
        if (glfwGetKey(window, GLFW_KEY_Q)     == GLFW_PRESS) camDist -= 0.05f;
        if (glfwGetKey(window, GLFW_KEY_E)     == GLFW_PRESS) camDist += 0.05f;
        camDist = glm::clamp(camDist, 2.0f, 15.0f);
        camElevation = glm::clamp(camElevation, -1.4f, 1.4f);

        // light position
        bool lightMoved = false;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { lightHeight += 0.03f; lightMoved = true; }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { lightHeight -= 0.03f; lightMoved = true; }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { lightAngle -= 0.03f;  lightMoved = true; }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { lightAngle += 0.03f;  lightMoved = true; }

        // disable auto-rotate on manual input
        static bool wasManual = false;
        if (lightMoved && !wasManual) { autoRotate = false; wasManual = true; }
        if (!lightMoved) wasManual = false;

        // toggle auto-rotate
        static bool rWasDown = false;
        bool rDown = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
        if (rDown && !rWasDown) { autoRotate = !autoRotate; cout << "auto-rotate: " << (autoRotate ? "ON" : "OFF") << endl; }
        rWasDown = rDown;

        // toggle Blinn-Phong / Phong
        static bool fWasDown = false;
        bool fDown = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
        if (fDown && !fWasDown) { blinnPhong = !blinnPhong; cout << "mode: " << (blinnPhong ? "Blinn-Phong" : "Phong") << endl; }
        fWasDown = fDown;

        // parameter adjustments
        static bool k1 = false, k2 = false, k3 = false, k4 = false;
        static bool k5 = false, k6 = false, k7 = false, k8 = false;
        bool d1 = glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS;
        bool d2 = glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS;
        bool d3 = glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS;
        bool d4 = glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS;
        bool d5 = glfwGetKey(window, GLFW_KEY_5) == GLFW_PRESS;
        bool d6 = glfwGetKey(window, GLFW_KEY_6) == GLFW_PRESS;
        bool d7 = glfwGetKey(window, GLFW_KEY_7) == GLFW_PRESS;
        bool d8 = glfwGetKey(window, GLFW_KEY_8) == GLFW_PRESS;

        if (d1 && !k1) { ambientStr  = glm::max(0.0f, ambientStr - 0.05f);  cout << "ambient:  " << ambientStr << endl; } k1 = d1;
        if (d2 && !k2) { ambientStr  = glm::min(1.0f, ambientStr + 0.05f);  cout << "ambient:  " << ambientStr << endl; } k2 = d2;
        if (d3 && !k3) { diffuseStr  = glm::max(0.0f, diffuseStr - 0.05f);  cout << "diffuse:  " << diffuseStr << endl; } k3 = d3;
        if (d4 && !k4) { diffuseStr  = glm::min(1.0f, diffuseStr + 0.05f);  cout << "diffuse:  " << diffuseStr << endl; } k4 = d4;
        if (d5 && !k5) { specularStr = glm::max(0.0f, specularStr - 0.05f); cout << "specular: " << specularStr << endl; } k5 = d5;
        if (d6 && !k6) { specularStr = glm::min(1.0f, specularStr + 0.05f); cout << "specular: " << specularStr << endl; } k6 = d6;
        if (d7 && !k7) { shininess   = glm::max(1.0f, shininess * 0.5f);    cout << "shininess:" << shininess << endl; }   k7 = d7;
        if (d8 && !k8) { shininess   = glm::min(256.0f, shininess * 2.0f);   cout << "shininess:" << shininess << endl; }   k8 = d8;

        // auto-rotate
        if (autoRotate) lightAngle += 0.01f;

        // ── compute positions ───────────────────────────────
        float lx = lightRadius * cosf(lightAngle);
        float lz = lightRadius * sinf(lightAngle);
        glm::vec3 lightPos(lx, lightHeight, lz);

        float cx = camDist * cosf(camElevation) * sinf(camAngle);
        float cy = camDist * sinf(camElevation);
        float cz = camDist * cosf(camElevation) * cosf(camAngle);
        glm::vec3 camPos(cx, cy, cz);

        glm::mat4 projection = glm::perspective(glm::radians(45.0f),
            (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);
        glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        // ── render ──────────────────────────────────────────
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ── lit objects ─────────────────────────────────────
        glUseProgram(litProg);
        glUniform3f(glGetUniformLocation(litProg, "uLightPos"), lightPos.x, lightPos.y, lightPos.z);
        glUniform3f(glGetUniformLocation(litProg, "uLightColor"), 1.0f, 0.95f, 0.85f);
        glUniform3f(glGetUniformLocation(litProg, "uViewPos"), camPos.x, camPos.y, camPos.z);
        glUniform1f(glGetUniformLocation(litProg, "uAmbientStrength"), ambientStr);
        glUniform1f(glGetUniformLocation(litProg, "uDiffuseStrength"), diffuseStr);
        glUniform1f(glGetUniformLocation(litProg, "uSpecularStrength"), specularStr);
        glUniform1f(glGetUniformLocation(litProg, "uShininess"), shininess);
        glUniform1i(glGetUniformLocation(litProg, "uBlinnPhong"), blinnPhong);

        // sphere
        {
            glm::mat4 model = glm::mat4(1.0f);
            glm::mat4 mvp = projection * view * model;
            glUniformMatrix4fv(glGetUniformLocation(litProg, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(litProg, "uMVP"),  1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(glGetUniformLocation(litProg, "uObjectColor"), 0.85f, 0.60f, 0.40f);
            glBindVertexArray(sphere.VAO);
            glDrawElements(GL_TRIANGLES, sphere.indexCount, GL_UNSIGNED_INT, 0);
        }

        // floor
        {
            glm::mat4 model = glm::mat4(1.0f);
            glm::mat4 mvp = projection * view * model;
            glUniformMatrix4fv(glGetUniformLocation(litProg, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(litProg, "uMVP"),  1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(glGetUniformLocation(litProg, "uObjectColor"), 0.25f, 0.27f, 0.32f);
            glBindVertexArray(floor.VAO);
            glDrawElements(GL_TRIANGLES, floor.indexCount, GL_UNSIGNED_INT, 0);
        }

        // ── light indicator ─────────────────────────────────
        glUseProgram(simpleProg);
        {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), lightPos);
            glm::mat4 mvp = projection * view * model;
            glUniformMatrix4fv(glGetUniformLocation(simpleProg, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(glGetUniformLocation(simpleProg, "uColor"), 1.0f, 0.95f, 0.3f);
            glBindVertexArray(cube.VAO);
            glDrawElements(GL_TRIANGLES, cube.indexCount, GL_UNSIGNED_INT, 0);
        }

        checkError();
        glfwSwapBuffers(window);
    }

    // cleanup
    for (auto* m : { &sphere, &floor, &cube }) {
        glDeleteVertexArrays(1, &m->VAO);
        glDeleteBuffers(1, &m->VBO);
        glDeleteBuffers(1, &m->EBO);
    }
    glDeleteProgram(litProg);
    glDeleteProgram(simpleProg);
    glfwTerminate();
    return 0;
}
