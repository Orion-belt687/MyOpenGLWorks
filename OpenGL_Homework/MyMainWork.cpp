#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/noise.hpp>
#include <glm/gtc/random.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

using namespace std;

// ============================================================
//  CONSTANTS
// ============================================================
const unsigned int SCR_WIDTH = 1600;
const unsigned int SCR_HEIGHT = 1000;
const float PI = 3.14159265359f;
const int   TEX_SIZE = 256;

// ============================================================
//  CAMERA & INPUT STATE
// ============================================================
struct CameraState {
    float azimuth = 0.5f;
    float elevation = 0.35f;
    float distance = 18.0f;
    glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);

    glm::vec3 position() const {
        float cx = distance * cosf(elevation) * sinf(azimuth);
        float cy = distance * sinf(elevation);
        float cz = distance * cosf(elevation) * cosf(azimuth);
        return glm::vec3(cx, cy, cz) + target;
    }
};

CameraState g_cam;
bool  g_mouseDragging = false;
float g_lastMouseX = 0, g_lastMouseY = 0;
bool  g_paused = false;
bool  g_ffdEnabled = true;
bool  g_keyframeEnabled = true;
bool  g_showLattice = true;
bool  g_blinnPhong = true;
float g_animTime = 0.0f;
float g_deltaTime = 0.0f;
int   g_focusPlanet = -1;

// ============================================================
//  SHADER SOURCES
// ============================================================

// --- planet shader (textured + Phong/Blinn-Phong) ---
const char* planetVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;

uniform mat4 uModel;
uniform mat4 uMVP;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vTexCoord = aTexCoord;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* planetFragSrc = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

uniform sampler2D uTexture;
uniform vec3 uLightPos;
uniform vec3 uLightColor;
uniform vec3 uViewPos;
uniform float uAmbientStrength;
uniform float uDiffuseStrength;
uniform float uSpecularStrength;
uniform float uShininess;
uniform bool uBlinnPhong;

void main() {
    vec3 texColor = texture(uTexture, vTexCoord).rgb;

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

    vec3 result = (ambient + diffuse) * texColor + specular;
    FragColor = vec4(result, 1.0);
}
)";

// --- sun shader (emissive) ---
const char* sunVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
out vec2 vTexCoord;
uniform mat4 uMVP;
void main() {
    vTexCoord = aTexCoord;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* sunFragSrc = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D uTexture;
void main() {
    vec3 texColor = texture(uTexture, vTexCoord).rgb;
    FragColor = vec4(texColor * 1.5, 1.0);
}
)";

// --- simple colored shader (ring, lattice, stars) ---
const char* colorVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
uniform mat4 uMVP;
void main() {
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* colorFragSrc = R"(
#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vColor;
}
)";

// ============================================================
//  MESH STRUCT & HELPERS
// ============================================================
struct Mesh {
    unsigned int VAO = 0;
    unsigned int VBO = 0;
    unsigned int EBO = 0;
    int indexCount = 0;
    int vertexCount = 0;
};

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

// ============================================================
//  MESH GENERATION
// ============================================================

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
            verts.push_back(radius * x);
            verts.push_back(radius * y);
            verts.push_back(radius * z);
            verts.push_back(x);
            verts.push_back(y);
            verts.push_back(z);
            verts.push_back((float)j / sectors);
            verts.push_back((float)i / stacks);
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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    return m;
}

Mesh generateRing(float innerR, float outerR, int segments) {
    vector<float> verts;
    vector<unsigned int> indices;

    for (int i = 0; i <= segments; i++) {
        float angle = 2.0f * PI * i / segments;
        float c = cosf(angle);
        float s = sinf(angle);
        verts.push_back(innerR * c);
        verts.push_back(0.0f);
        verts.push_back(innerR * s);
        verts.push_back(0.76f); verts.push_back(0.70f); verts.push_back(0.60f); verts.push_back(0.85f);
        verts.push_back(outerR * c);
        verts.push_back(0.0f);
        verts.push_back(outerR * s);
        float ringVar = 0.5f + 0.5f * sinf(angle * 17.0f) * cosf(angle * 11.0f);
        float alpha = 0.3f + 0.7f * ringVar;
        verts.push_back(0.70f); verts.push_back(0.62f); verts.push_back(0.50f); verts.push_back(alpha);
    }

    for (int i = 0; i < segments; i++) {
        unsigned int i1 = i * 2, i2 = i * 2 + 1;
        unsigned int i3 = (i + 1) * 2, i4 = (i + 1) * 2 + 1;
        indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
        indices.push_back(i2); indices.push_back(i4); indices.push_back(i3);
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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    return m;
}

Mesh generateStarfield(int count, float radius) {
    vector<float> verts;
    for (int i = 0; i < count; i++) {
        float phi_ = (1.0f + sqrtf(5.0f)) / 2.0f;
        float theta = 2.0f * PI * i / phi_;
        float phi = acosf(1.0f - 2.0f * (float(i) + 0.5f) / float(count));
        float x = sinf(phi) * cosf(theta);
        float y = sinf(phi) * sinf(theta);
        float z = cosf(phi);
        verts.push_back(radius * x);
        verts.push_back(radius * y);
        verts.push_back(radius * z);
        float brightness = 0.5f + 0.5f * fmodf(i * 127.1f + 311.7f, 1.0f);
        verts.push_back(brightness);
        verts.push_back(brightness);
        verts.push_back(brightness * 0.9f);
        verts.push_back(1.0f);
    }

    Mesh m;
    m.vertexCount = count;
    glGenVertexArrays(1, &m.VAO);
    glGenBuffers(1, &m.VBO);
    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    return m;
}

// ============================================================
//  PROCEDURAL TEXTURE GENERATION
// ============================================================

float fract_(float v) { return v - floorf(v); }
float hash_(float x, float y) {
    return fract_(sinf(x * 127.1f + y * 311.7f) * 43758.5453f);
}
float smoothNoise(float x, float y) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    float sx = fx * fx * (3.0f - 2.0f * fx);
    float sy = fy * fy * (3.0f - 2.0f * fy);
    float n00 = hash_((float)ix, (float)iy);
    float n10 = hash_((float)ix + 1, (float)iy);
    float n01 = hash_((float)ix, (float)iy + 1);
    float n11 = hash_((float)ix + 1, (float)iy + 1);
    float nx0 = n00 + (n10 - n00) * sx;
    float nx1 = n01 + (n11 - n01) * sx;
    return nx0 + (nx1 - nx0) * sy;
}
float fbm(float x, float y, int octaves) {
    float val = 0, amp = 1.0f, freq = 1.0f, total = 0;
    for (int i = 0; i < octaves; i++) {
        val += amp * smoothNoise(x * freq, y * freq);
        total += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return val / total;
}

GLuint uploadTexture(const vector<unsigned char>& data, int size) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return tex;
}

GLuint generateSunTexture() {
    vector<unsigned char> data(TEX_SIZE * TEX_SIZE * 3);
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            float nx = (float)x / TEX_SIZE;
            float ny = (float)y / TEX_SIZE;
            float n = fbm(nx * 8.0f, ny * 8.0f, 5);
            float n2 = fbm(nx * 13.0f + 5.3f, ny * 13.0f + 2.1f, 4);
            float brightness = 0.6f + 0.4f * n;
            float spot = (n2 < 0.35f) ? 0.5f : 1.0f;
            unsigned char r = (unsigned char)(255.0f * glm::clamp(brightness, 0.0f, 1.0f));
            unsigned char g = (unsigned char)(255.0f * glm::clamp(brightness * 0.75f * spot, 0.0f, 1.0f));
            unsigned char b_ = (unsigned char)(255.0f * glm::clamp(brightness * 0.2f * spot, 0.0f, 1.0f));
            int idx = (y * TEX_SIZE + x) * 3;
            data[idx] = r; data[idx + 1] = g; data[idx + 2] = b_;
        }
    }
    return uploadTexture(data, TEX_SIZE);
}

GLuint generateRockyTexture(float r, float g, float b, float nScale, int octaves) {
    vector<unsigned char> data(TEX_SIZE * TEX_SIZE * 3);
    for (int y_ = 0; y_ < TEX_SIZE; y_++) {
        for (int x_ = 0; x_ < TEX_SIZE; x_++) {
            float nx = (float)x_ / TEX_SIZE;
            float ny = (float)y_ / TEX_SIZE;
            float elevation = fbm(nx * nScale, ny * nScale, octaves);
            float landMask = (elevation > 0.45f) ? 1.0f : 0.0f;
            float ocean = 0.25f + 0.15f * elevation;
            float land = 0.55f + 0.35f * elevation;
            float brightness = ocean + (land - ocean) * landMask;
            float detail = fbm(nx * nScale * 3.0f, ny * nScale * 3.0f, 3);
            brightness += 0.08f * detail;
            unsigned char rc = (unsigned char)(255.0f * glm::clamp(r * brightness, 0.0f, 1.0f));
            unsigned char gc = (unsigned char)(255.0f * glm::clamp(g * brightness, 0.0f, 1.0f));
            unsigned char bc = (unsigned char)(255.0f * glm::clamp(b * brightness, 0.0f, 1.0f));
            int idx = (y_ * TEX_SIZE + x_) * 3;
            data[idx] = rc; data[idx + 1] = gc; data[idx + 2] = bc;
        }
    }
    return uploadTexture(data, TEX_SIZE);
}

GLuint generateGasGiantTexture() {
    vector<unsigned char> data(TEX_SIZE * TEX_SIZE * 3);
    for (int y_ = 0; y_ < TEX_SIZE; y_++) {
        for (int x_ = 0; x_ < TEX_SIZE; x_++) {
            float nx = (float)x_ / TEX_SIZE;
            float ny = (float)y_ / TEX_SIZE - 0.5f;
            float band = sinf(ny * 18.0f + fbm(nx * 4.0f, ny * 6.0f, 3) * 1.5f);
            float band2 = sinf(ny * 9.0f + 1.3f + fbm(nx * 3.0f + 2.0f, ny * 4.0f, 3) * 1.0f);
            float turbulence = fbm(nx * 6.0f, ny * 6.0f, 4);
            float mix1 = band * 0.5f + 0.5f;
            float mix2 = band2 * 0.5f + 0.5f;
            float mix3 = turbulence * 0.5f + 0.5f;
            glm::vec3 c1(0.90f, 0.85f, 0.70f);
            glm::vec3 c2(0.82f, 0.60f, 0.40f);
            glm::vec3 c3(0.65f, 0.45f, 0.30f);
            glm::vec3 color = glm::mix(c1, c2, mix1);
            color = glm::mix(color, c3, mix2 * 0.4f);
            color += turbulence * 0.06f;
            // Great Red Spot
            float spotDist = sqrtf((nx - 0.65f) * (nx - 0.65f) + (ny + 0.1f - 0.5f + 0.5f) * (ny + 0.1f - 0.5f + 0.5f));
            float spot = expf(-spotDist * spotDist * 120.0f);
            color = glm::mix(color, glm::vec3(0.85f, 0.35f, 0.25f), spot * 0.7f);
            unsigned char rc = (unsigned char)(255.0f * glm::clamp(color.r, 0.0f, 1.0f));
            unsigned char gc = (unsigned char)(255.0f * glm::clamp(color.g, 0.0f, 1.0f));
            unsigned char bc = (unsigned char)(255.0f * glm::clamp(color.b, 0.0f, 1.0f));
            int idx = (y_ * TEX_SIZE + x_) * 3;
            data[idx] = rc; data[idx + 1] = gc; data[idx + 2] = bc;
        }
    }
    return uploadTexture(data, TEX_SIZE);
}

GLuint generateMoonTexture() {
    vector<unsigned char> data(TEX_SIZE * TEX_SIZE * 3);
    for (int y_ = 0; y_ < TEX_SIZE; y_++) {
        for (int x_ = 0; x_ < TEX_SIZE; x_++) {
            float nx = (float)x_ / TEX_SIZE;
            float ny = (float)y_ / TEX_SIZE;
            float n = fbm(nx * 7.0f, ny * 7.0f, 5);
            float crater = 0.0f;
            for (int ci = 0; ci < 8; ci++) {
                float cx = fmodf(ci * 137.508f, 1.0f);
                float cy = fmodf(ci * 263.147f, 1.0f);
                float cr = 0.03f + 0.07f * fmodf(ci * 97.133f, 1.0f);
                float d = sqrtf((nx - cx) * (nx - cx) + (ny - cy) * (ny - cy));
                if (d < cr) crater += (1.0f - d / cr) * 0.5f;
            }
            float brightness = 0.5f + 0.4f * n - crater;
            brightness = glm::clamp(brightness, 0.0f, 1.0f);
            unsigned char c = (unsigned char)(255.0f * brightness);
            int idx = (y_ * TEX_SIZE + x_) * 3;
            data[idx] = c; data[idx + 1] = c; data[idx + 2] = c;
        }
    }
    return uploadTexture(data, TEX_SIZE);
}

// ============================================================
//  FREE-FORM DEFORMATION (3x3x3 Bezier lattice)
// ============================================================
struct FFDLattice {
    glm::vec3 cp[3][3][3];
    glm::vec3 originalCp[3][3][3];
    glm::vec3 minBounds, maxBounds;

    void init(float halfSize) {
        minBounds = glm::vec3(-halfSize);
        maxBounds = glm::vec3(halfSize);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                for (int k = 0; k < 3; k++) {
                    float x = minBounds.x + (maxBounds.x - minBounds.x) * i / 2.0f;
                    float y = minBounds.y + (maxBounds.y - minBounds.y) * j / 2.0f;
                    float z = minBounds.z + (maxBounds.z - minBounds.z) * k / 2.0f;
                    cp[i][j][k] = glm::vec3(x, y, z);
                    originalCp[i][j][k] = cp[i][j][k];
                }
    }

    void reset() {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                for (int k = 0; k < 3; k++)
                    cp[i][j][k] = originalCp[i][j][k];
    }

    glm::vec3 toLattice(const glm::vec3& p) const {
        return glm::vec3(
            (p.x - minBounds.x) / (maxBounds.x - minBounds.x),
            (p.y - minBounds.y) / (maxBounds.y - minBounds.y),
            (p.z - minBounds.z) / (maxBounds.z - minBounds.z)
        );
    }

    static float B(int i, float t) {
        if (i == 0) return (1.0f - t) * (1.0f - t);
        if (i == 1) return 2.0f * (1.0f - t) * t;
        return t * t;
    }
    static float Bderiv(int i, float t) {
        if (i == 0) return -2.0f * (1.0f - t);
        if (i == 1) return 2.0f - 4.0f * t;
        return 2.0f * t;
    }

    glm::vec3 deform(const glm::vec3& p) const {
        glm::vec3 luv = toLattice(p);
        float s = glm::clamp(luv.x, 0.0f, 1.0f);
        float t = glm::clamp(luv.y, 0.0f, 1.0f);
        float u = glm::clamp(luv.z, 0.0f, 1.0f);
        glm::vec3 result(0.0f);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                for (int k = 0; k < 3; k++)
                    result += B(i, s) * B(j, t) * B(k, u) * cp[i][j][k];
        return result;
    }

    glm::vec3 deformNormal(const glm::vec3& p, const glm::vec3& n) const {
        glm::vec3 luv = toLattice(p);
        float s = glm::clamp(luv.x, 0.0f, 1.0f);
        float t = glm::clamp(luv.y, 0.0f, 1.0f);
        float u = glm::clamp(luv.z, 0.0f, 1.0f);
        auto evalDeriv = [&](int dim) {
            glm::vec3 d(0.0f);
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    for (int k = 0; k < 3; k++) {
                        float w;
                        if (dim == 0) w = Bderiv(i, s) * B(j, t) * B(k, u);
                        else if (dim == 1) w = B(i, s) * Bderiv(j, t) * B(k, u);
                        else w = B(i, s) * B(j, t) * Bderiv(k, u);
                        d += w * cp[i][j][k];
                    }
            return d;
        };
        glm::vec3 dPds = evalDeriv(0);
        glm::vec3 dPdt = evalDeriv(1);
        glm::vec3 dPdu = evalDeriv(2);
        glm::mat3 J(dPds, dPdt, dPdu);
        return glm::normalize(glm::transpose(glm::inverse(J)) * n);
    }
};

FFDLattice g_ffd;

// ============================================================
//  KEYFRAME SYSTEM
// ============================================================
struct Keyframe {
    float time;
    glm::vec3 offsets[3][3][3];
};

vector<Keyframe> g_keyframes;
float g_keyframeDuration = 8.0f;

void initKeyframes() {
    g_keyframes.clear();
    // keyframe 0: rest pose
    {
        Keyframe kf; kf.time = 0.0f;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                for (int k = 0; k < 3; k++)
                    kf.offsets[i][j][k] = glm::vec3(0.0f);
        g_keyframes.push_back(kf);
    }
    // keyframe 1: vertical stretch
    {
        Keyframe kf; kf.time = 2.0f;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                for (int k = 0; k < 3; k++) {
                    kf.offsets[i][j][k] = glm::vec3(0.0f);
                    if (j == 2) kf.offsets[i][j][k].y = 0.35f;
                    if (j == 0) kf.offsets[i][j][k].y = -0.35f;
                }
        g_keyframes.push_back(kf);
    }
    // keyframe 2: equatorial bulge
    {
        Keyframe kf; kf.time = 4.0f;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                for (int k = 0; k < 3; k++) {
                    kf.offsets[i][j][k] = glm::vec3(0.0f);
                    if (j == 1) {
                        kf.offsets[i][j][k].x = (i == 0 ? -0.3f : (i == 2 ? 0.3f : 0.0f));
                        kf.offsets[i][j][k].z = (k == 0 ? -0.3f : (k == 2 ? 0.3f : 0.0f));
                    }
                }
        g_keyframes.push_back(kf);
    }
    // keyframe 3: asymmetric twist
    {
        Keyframe kf; kf.time = 6.0f;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                for (int k = 0; k < 3; k++) {
                    kf.offsets[i][j][k] = glm::vec3(0.0f);
                    if (i == 2 && j == 2 && k == 2) kf.offsets[i][j][k] = glm::vec3(0.25f, 0.25f, 0.25f);
                    if (i == 0 && j == 0 && k == 0) kf.offsets[i][j][k] = glm::vec3(-0.2f, -0.2f, -0.2f);
                    if (i == 1 && j == 2 && k == 1) kf.offsets[i][j][k] = glm::vec3(0.0f, 0.3f, 0.0f);
                }
        g_keyframes.push_back(kf);
    }
    // keyframe 4: back to rest
    {
        Keyframe kf; kf.time = 8.0f;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                for (int k = 0; k < 3; k++)
                    kf.offsets[i][j][k] = glm::vec3(0.0f);
        g_keyframes.push_back(kf);
    }
}

void evaluateKeyframes(float time, glm::vec3 offsets[3][3][3]) {
    if (g_keyframes.empty()) return;
    float t = fmodf(time, g_keyframeDuration);
    int idx0 = 0, idx1 = 0;
    for (int i = 0; i < (int)g_keyframes.size(); i++) {
        if (g_keyframes[i].time <= t) idx0 = i;
        if (g_keyframes[i].time >= t) { idx1 = i; break; }
    }
    if (idx0 == idx1) {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                for (int k = 0; k < 3; k++)
                    offsets[i][j][k] = g_keyframes[idx0].offsets[i][j][k];
        return;
    }
    int nextIdx = (idx0 + 1) % g_keyframes.size();
    if (nextIdx != idx1 && idx1 == 0) idx1 = nextIdx;
    float t0 = g_keyframes[idx0].time;
    float t1 = g_keyframes[idx1].time;
    if (t1 < t0) t1 += g_keyframeDuration;
    float localT = (t - t0) / (t1 - t0);
    float st = localT * localT * (3.0f - 2.0f * localT);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++)
                offsets[i][j][k] = glm::mix(g_keyframes[idx0].offsets[i][j][k],
                                           g_keyframes[idx1].offsets[i][j][k], st);
}

// ============================================================
//  DYNAMIC MESH FOR FFD PLANET
// ============================================================
struct DynamicMesh {
    unsigned int VAO = 0, VBO = 0, EBO = 0;
    int indexCount = 0;
    int vertexCount = 0;
    vector<float> originalVerts;

    void initFromSphere(float radius, int sectors, int stacks) {
        vector<unsigned int> indices;
        originalVerts.clear();
        for (int i = 0; i <= stacks; i++) {
            float phi = PI * i / stacks;
            float y_ = cosf(phi);
            float r = sinf(phi);
            for (int j = 0; j <= sectors; j++) {
                float theta = 2.0f * PI * j / sectors;
                float x = r * cosf(theta);
                float z = r * sinf(theta);
                originalVerts.push_back(radius * x);
                originalVerts.push_back(radius * y_);
                originalVerts.push_back(radius * z);
                originalVerts.push_back(x);
                originalVerts.push_back(y_);
                originalVerts.push_back(z);
                originalVerts.push_back((float)j / sectors);
                originalVerts.push_back((float)i / stacks);
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
        vertexCount = (int)(originalVerts.size() / 8);
        indexCount = (int)indices.size();
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, originalVerts.size() * sizeof(float), originalVerts.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    }

    void updateFFD(const FFDLattice& ffd, bool ffdActive) {
        vector<float> deformed = originalVerts;
        for (int v = 0; v < vertexCount; v++) {
            int base = v * 8;
            glm::vec3 pos(originalVerts[base], originalVerts[base + 1], originalVerts[base + 2]);
            glm::vec3 norm(originalVerts[base + 3], originalVerts[base + 4], originalVerts[base + 5]);
            if (ffdActive) {
                glm::vec3 newPos = ffd.deform(pos);
                glm::vec3 newNorm = ffd.deformNormal(pos, norm);
                deformed[base] = newPos.x;
                deformed[base + 1] = newPos.y;
                deformed[base + 2] = newPos.z;
                deformed[base + 3] = newNorm.x;
                deformed[base + 4] = newNorm.y;
                deformed[base + 5] = newNorm.z;
            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, deformed.size() * sizeof(float), deformed.data());
    }
};

// ============================================================
//  LATTICE VISUALIZATION
// ============================================================
struct LatticeVis {
    unsigned int pointVAO = 0, pointVBO = 0;
    unsigned int lineVAO = 0, lineVBO = 0;
    int pointCount = 27;
    int lineCount = 0;
};

LatticeVis g_latticeVis;

void updateLatticeVis(const FFDLattice& ffd) {
    vector<float> pts;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++) {
                pts.push_back(ffd.cp[i][j][k].x);
                pts.push_back(ffd.cp[i][j][k].y);
                pts.push_back(ffd.cp[i][j][k].z);
                pts.push_back(0.2f); pts.push_back(1.0f); pts.push_back(0.3f); pts.push_back(1.0f);
            }

    vector<float> lines;
    auto addEdge = [&](const glm::vec3& a, const glm::vec3& b) {
        lines.push_back(a.x); lines.push_back(a.y); lines.push_back(a.z);
        lines.push_back(0.2f); lines.push_back(1.0f); lines.push_back(0.3f); lines.push_back(0.7f);
        lines.push_back(b.x); lines.push_back(b.y); lines.push_back(b.z);
        lines.push_back(0.2f); lines.push_back(1.0f); lines.push_back(0.3f); lines.push_back(0.7f);
    };

    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 2; k++)
                addEdge(ffd.cp[i][j][k], ffd.cp[i][j][k + 1]);
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 3; k++)
            for (int j = 0; j < 2; j++)
                addEdge(ffd.cp[i][j][k], ffd.cp[i][j + 1][k]);
    for (int j = 0; j < 3; j++)
        for (int k = 0; k < 3; k++)
            for (int i = 0; i < 2; i++)
                addEdge(ffd.cp[i][j][k], ffd.cp[i + 1][j][k]);

    glBindBuffer(GL_ARRAY_BUFFER, g_latticeVis.pointVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, pts.size() * sizeof(float), pts.data());

    g_latticeVis.lineCount = (int)(lines.size() / 7);
    glBindBuffer(GL_ARRAY_BUFFER, g_latticeVis.lineVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, lines.size() * sizeof(float), lines.data());
}

void initLatticeVis() {
    glGenVertexArrays(1, &g_latticeVis.pointVAO);
    glGenBuffers(1, &g_latticeVis.pointVBO);
    glBindVertexArray(g_latticeVis.pointVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_latticeVis.pointVBO);
    glBufferData(GL_ARRAY_BUFFER, 27 * 7 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    glGenVertexArrays(1, &g_latticeVis.lineVAO);
    glGenBuffers(1, &g_latticeVis.lineVBO);
    glBindVertexArray(g_latticeVis.lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_latticeVis.lineVBO);
    glBufferData(GL_ARRAY_BUFFER, 54 * 2 * 7 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

// ============================================================
//  PLANET DATA
// ============================================================
struct Planet {
    string name;
    float orbitRadius = 0.0f;
    float orbitSpeed = 0.0f;
    float orbitAngle = 0.0f;
    float rotationSpeed = 0.0f;
    float rotationAngle = 0.0f;
    float size = 0.0f;
    GLuint texture = 0;
    bool emissive = false;
    bool hasRing = false;
    float ringInner = 0, ringOuter = 0;
    float ringTilt = 0;
    bool isFFDTarget = false;
    bool isMoon = false;
    int parentPlanet = -1;
    float moonOrbitRadius = 0;
    float moonOrbitSpeed = 0;
    float moonOrbitAngle = 0;
};

vector<Planet> g_planets;

void initPlanets(GLuint sunTex, GLuint rockyTex1, GLuint rockyTex2, GLuint gasTex, GLuint moonTex) {
    Planet sun;
    sun.name = "Sun";
    sun.orbitRadius = 0.0f;
    sun.orbitSpeed = 0.0f;
    sun.rotationSpeed = 0.15f;
    sun.size = 1.5f;
    sun.texture = sunTex;
    sun.emissive = true;
    g_planets.push_back(sun);

    Planet r1;
    r1.name = "Rocky-1";
    r1.orbitRadius = 4.5f;
    r1.orbitSpeed = 0.5f;
    r1.rotationSpeed = 1.0f;
    r1.size = 0.4f;
    r1.texture = rockyTex1;
    g_planets.push_back(r1);

    Planet moon;
    moon.name = "Moon";
    moon.rotationSpeed = 0.3f;
    moon.size = 0.12f;
    moon.texture = moonTex;
    moon.isMoon = true;
    moon.parentPlanet = 1;
    moon.moonOrbitRadius = 0.75f;
    moon.moonOrbitSpeed = 2.5f;
    g_planets.push_back(moon);

    Planet r2;
    r2.name = "Rocky-2";
    r2.orbitRadius = 7.0f;
    r2.orbitSpeed = 0.35f;
    r2.orbitAngle = 1.5f;
    r2.rotationSpeed = 0.8f;
    r2.size = 0.45f;
    r2.texture = rockyTex2;
    r2.isFFDTarget = true;
    g_planets.push_back(r2);

    Planet gas;
    gas.name = "GasGiant";
    gas.orbitRadius = 10.0f;
    gas.orbitSpeed = 0.22f;
    gas.orbitAngle = 3.0f;
    gas.rotationSpeed = 0.6f;
    gas.size = 0.95f;
    gas.texture = gasTex;
    gas.hasRing = true;
    gas.ringInner = 1.3f;
    gas.ringOuter = 1.8f;
    gas.ringTilt = 25.0f;
    g_planets.push_back(gas);
}

// ============================================================
//  CALLBACKS
// ============================================================
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        g_mouseDragging = (action == GLFW_PRESS);
        if (g_mouseDragging) {
            double x, y;
            glfwGetCursorPos(window, &x, &y);
            g_lastMouseX = (float)x;
            g_lastMouseY = (float)y;
        }
    }
}

void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    if (g_mouseDragging) {
        float dx = (float)xpos - g_lastMouseX;
        float dy = (float)ypos - g_lastMouseY;
        g_cam.azimuth -= dx * 0.005f;
        g_cam.elevation += dy * 0.005f;
        g_cam.elevation = glm::clamp(g_cam.elevation, -1.5f, 1.5f);
        g_lastMouseX = (float)xpos;
        g_lastMouseY = (float)ypos;
    }
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    g_cam.distance -= (float)yoffset * 0.8f;
    g_cam.distance = glm::clamp(g_cam.distance, 3.0f, 50.0f);
}

// ============================================================
//  MAIN
// ============================================================
int main() {
    cout << "=== Solar System Demo ===" << endl;
    cout << "Features: Star, 2 rocky planets, 1 moon, 1 gas giant with ring" << endl;
    cout << "          Phong/Blinn-Phong lighting, procedural textures" << endl;
    cout << "          Free-Form Deformation (FFD), Keyframe animation" << endl;
    cout << endl;
    cout << "Controls:" << endl;
    cout << "  Left Mouse Drag : orbit camera" << endl;
    cout << "  Mouse Wheel     : zoom in/out" << endl;
    cout << "  W/A/S/D         : pan camera target" << endl;
    cout << "  Space           : pause/resume animation" << endl;
    cout << "  F               : toggle FFD deformation" << endl;
    cout << "  K               : toggle keyframe animation" << endl;
    cout << "  L               : toggle FFD lattice display" << endl;
    cout << "  B               : toggle Blinn-Phong / Phong" << endl;
    cout << "  1/2/3/4/5       : focus on Sun/Rocky1/Moon/Rocky2/GasGiant" << endl;
    cout << "  0               : free camera" << endl;
    cout << "  R               : reset camera" << endl;
    cout << "  ESC             : close window" << endl;

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Solar System - FFD & Keyframes", NULL, NULL);
    if (!window) {
        cout << "Failed to create GLFW window" << endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        cout << "Failed to initialize GLAD" << endl;
        return -1;
    }

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    glClearColor(0.02f, 0.02f, 0.06f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // compile shaders
    unsigned int planetVS = compileShader(GL_VERTEX_SHADER, planetVertSrc);
    unsigned int planetFS = compileShader(GL_FRAGMENT_SHADER, planetFragSrc);
    unsigned int planetProg = linkProgram(planetVS, planetFS);
    glDeleteShader(planetVS); glDeleteShader(planetFS);

    unsigned int sunVS = compileShader(GL_VERTEX_SHADER, sunVertSrc);
    unsigned int sunFS = compileShader(GL_FRAGMENT_SHADER, sunFragSrc);
    unsigned int sunProg = linkProgram(sunVS, sunFS);
    glDeleteShader(sunVS); glDeleteShader(sunFS);

    unsigned int colorVS = compileShader(GL_VERTEX_SHADER, colorVertSrc);
    unsigned int colorFS = compileShader(GL_FRAGMENT_SHADER, colorFragSrc);
    unsigned int colorProg = linkProgram(colorVS, colorFS);
    glDeleteShader(colorVS); glDeleteShader(colorFS);

    // generate textures
    GLuint sunTex = generateSunTexture();
    GLuint rockyTex1 = generateRockyTexture(0.3f, 0.55f, 0.8f, 5.0f, 5);
    GLuint rockyTex2 = generateRockyTexture(0.75f, 0.35f, 0.2f, 6.0f, 5);
    GLuint gasTex = generateGasGiantTexture();
    GLuint moonTex = generateMoonTexture();

    // generate meshes
    Mesh sphere = generateSphere(1.0f, 48, 24);
    DynamicMesh ffdMesh;
    ffdMesh.initFromSphere(1.0f, 48, 24);
    Mesh ringMesh = generateRing(1.0f, 1.4f, 200);
    Mesh starfield = generateStarfield(2000, 55.0f);

    // init FFD, keyframes, lattice
    g_ffd.init(0.6f);
    initKeyframes();
    initLatticeVis();
    initPlanets(sunTex, rockyTex1, rockyTex2, gasTex, moonTex);

    // lighting
    float ambientStr = 0.12f;
    float diffuseStr = 0.75f;
    float specularStr = 0.45f;
    float shininess = 32.0f;

    float lastFrameTime = (float)glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        float currentTime = (float)glfwGetTime();
        g_deltaTime = currentTime - lastFrameTime;
        lastFrameTime = currentTime;

        if (g_paused)
            g_animTime += g_deltaTime;

        glfwPollEvents();

        // keyboard
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        float panSpeed = 0.05f;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) g_cam.target.z -= panSpeed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) g_cam.target.z += panSpeed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) g_cam.target.x -= panSpeed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) g_cam.target.x += panSpeed;

        // edge-triggered toggles
        static bool spWas = false, fWas = false, kWas = false, lWas = false, bWas = false, rWas = false;
        bool sp = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        bool fk = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
        bool kk = glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS;
        bool lk = glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS;
        bool bk = glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS;
        bool rk = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
        if (sp && !spWas) { g_paused = !g_paused; cout << (g_paused ? "PAUSED" : "PLAYING") << endl; }
        if (fk && !fWas) { g_ffdEnabled = !g_ffdEnabled; cout << "FFD: " << (g_ffdEnabled ? "ON" : "OFF") << endl; }
        if (kk && !kWas) { g_keyframeEnabled = !g_keyframeEnabled; cout << "Keyframes: " << (g_keyframeEnabled ? "ON" : "OFF") << endl; }
        if (lk && !lWas) { g_showLattice = !g_showLattice; cout << "Lattice: " << (g_showLattice ? "ON" : "OFF") << endl; }
        if (bk && !bWas) { g_blinnPhong = !g_blinnPhong; cout << "Lighting: " << (g_blinnPhong ? "Blinn-Phong" : "Phong") << endl; }
        if (rk && !rWas) { g_cam = CameraState(); cout << "Camera reset" << endl; }
        spWas = sp; fWas = fk; kWas = kk; lWas = lk; bWas = bk; rWas = rk;

        // focus keys
        static bool k0w = false, k1w = false, k2w = false, k3w = false, k4w = false, k5w = false;
        bool k0 = glfwGetKey(window, GLFW_KEY_0) == GLFW_PRESS;
        bool k1 = glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS;
        bool k2 = glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS;
        bool k3 = glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS;
        bool k4 = glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS;
        bool k5 = glfwGetKey(window, GLFW_KEY_5) == GLFW_PRESS;
        if (k0 && !k0w) g_focusPlanet = -1;
        if (k1 && !k1w) g_focusPlanet = 0;
        if (k2 && !k2w) g_focusPlanet = 1;
        if (k3 && !k3w) g_focusPlanet = 2;
        if (k4 && !k4w) g_focusPlanet = 3;
        if (k5 && !k5w) g_focusPlanet = 4;
        k0w = k0; k1w = k1; k2w = k2; k3w = k3; k4w = k4; k5w = k5;

        // update planet positions
        for (auto& p : g_planets) {
            if (!p.isMoon) {
                p.orbitAngle += p.orbitSpeed * g_deltaTime;
                p.rotationAngle += p.rotationSpeed * g_deltaTime;
            } else {
                p.moonOrbitAngle += p.moonOrbitSpeed * g_deltaTime;
                p.rotationAngle += p.rotationSpeed * g_deltaTime;
            }
        }

        vector<glm::vec3> planetPositions(g_planets.size());
        for (size_t i = 0; i < g_planets.size(); i++) {
            if (g_planets[i].isMoon) {
                int parent = g_planets[i].parentPlanet;
                glm::vec3 parentPos = planetPositions[parent];
                float ma = g_planets[i].moonOrbitAngle;
                float mr = g_planets[i].moonOrbitRadius;
                planetPositions[i] = parentPos + glm::vec3(mr * cosf(ma), 0.0f, mr * sinf(ma));
            } else {
                float a = g_planets[i].orbitAngle;
                float r = g_planets[i].orbitRadius;
                planetPositions[i] = glm::vec3(r * cosf(a), 0.0f, r * sinf(a));
            }
        }

        if (g_focusPlanet >= 0 && g_focusPlanet < (int)g_planets.size())
            g_cam.target = planetPositions[g_focusPlanet];

        glm::vec3 lightPos = planetPositions[0]; // sun

        // update FFD from keyframes
        if (g_keyframeEnabled) {
            glm::vec3 offsets[3][3][3];
            evaluateKeyframes(g_animTime, offsets);
            g_ffd.reset();
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    for (int k = 0; k < 3; k++)
                        g_ffd.cp[i][j][k] += offsets[i][j][k];
        } else if (!g_ffdEnabled) {
            g_ffd.reset();
        }

        // update dynamic mesh and lattice
        ffdMesh.updateFFD(g_ffd, g_ffdEnabled);
        updateLatticeVis(g_ffd);

        // camera
        glm::vec3 camPos = g_cam.position();
        glm::mat4 projection = glm::perspective(glm::radians(45.0f),
            (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 200.0f);
        glm::mat4 view = glm::lookAt(camPos, g_cam.target, glm::vec3(0.0f, 1.0f, 0.0f));

        // render
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // starfield (no depth write)
        glDepthMask(GL_FALSE);
        glUseProgram(colorProg);
        {
            glm::mat4 mvp = projection * view * glm::mat4(1.0f);
            glUniformMatrix4fv(glGetUniformLocation(colorProg, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
            glBindVertexArray(starfield.VAO);
            glPointSize(2.0f);
            glDrawArrays(GL_POINTS, 0, starfield.vertexCount);
        }
        glDepthMask(GL_TRUE);

        // planets
        for (size_t i = 0; i < g_planets.size(); i++) {
            const auto& p = g_planets[i];
            glm::vec3 pos = planetPositions[i];
            float scale = p.size;

            if (p.emissive) {
                // SUN
                glUseProgram(sunProg);
                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, pos);
                model = glm::rotate(model, p.rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
                model = glm::scale(model, glm::vec3(scale));
                glm::mat4 mvp = projection * view * model;
                glUniformMatrix4fv(glGetUniformLocation(sunProg, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, p.texture);
                glUniform1i(glGetUniformLocation(sunProg, "uTexture"), 0);
                glBindVertexArray(sphere.VAO);
                glDrawElements(GL_TRIANGLES, sphere.indexCount, GL_UNSIGNED_INT, 0);
            } else {
                // PLANETS & MOONS
                glUseProgram(planetProg);
                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, pos);
                model = glm::rotate(model, p.rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
                model = glm::scale(model, glm::vec3(scale));
                glm::mat4 mvp = projection * view * model;

                glUniformMatrix4fv(glGetUniformLocation(planetProg, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
                glUniformMatrix4fv(glGetUniformLocation(planetProg, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
                glUniform3f(glGetUniformLocation(planetProg, "uLightPos"), lightPos.x, lightPos.y, lightPos.z);
                glUniform3f(glGetUniformLocation(planetProg, "uLightColor"), 1.0f, 0.95f, 0.85f);
                glUniform3f(glGetUniformLocation(planetProg, "uViewPos"), camPos.x, camPos.y, camPos.z);
                glUniform1f(glGetUniformLocation(planetProg, "uAmbientStrength"), ambientStr);
                glUniform1f(glGetUniformLocation(planetProg, "uDiffuseStrength"), diffuseStr);
                glUniform1f(glGetUniformLocation(planetProg, "uSpecularStrength"), specularStr);
                glUniform1f(glGetUniformLocation(planetProg, "uShininess"), shininess);
                glUniform1i(glGetUniformLocation(planetProg, "uBlinnPhong"), g_blinnPhong);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, p.texture);
                glUniform1i(glGetUniformLocation(planetProg, "uTexture"), 0);

                if (p.isFFDTarget) {
                    glBindVertexArray(ffdMesh.VAO);
                    glDrawElements(GL_TRIANGLES, ffdMesh.indexCount, GL_UNSIGNED_INT, 0);
                } else {
                    glBindVertexArray(sphere.VAO);
                    glDrawElements(GL_TRIANGLES, sphere.indexCount, GL_UNSIGNED_INT, 0);
                }
            }

            // RING
            if (p.hasRing) {
                glUseProgram(colorProg);
                glm::mat4 ringModel = glm::mat4(1.0f);
                ringModel = glm::translate(ringModel, pos);
                ringModel = glm::rotate(ringModel, glm::radians(p.ringTilt), glm::vec3(1.0f, 0.0f, 0.0f));
                ringModel = glm::rotate(ringModel, p.rotationAngle * 0.3f, glm::vec3(0.0f, 1.0f, 0.0f));
                float ringS = p.size;
                ringModel = glm::scale(ringModel, glm::vec3(ringS));
                glm::mat4 ringMVP = projection * view * ringModel;
                glUniformMatrix4fv(glGetUniformLocation(colorProg, "uMVP"), 1, GL_FALSE, glm::value_ptr(ringMVP));
                glBindVertexArray(ringMesh.VAO);
                glDrawElements(GL_TRIANGLES, ringMesh.indexCount, GL_UNSIGNED_INT, 0);
            }
        }

        // FFD lattice
        if (g_showLattice && (g_ffdEnabled || g_keyframeEnabled)) {
            for (size_t i = 0; i < g_planets.size(); i++) {
                if (g_planets[i].isFFDTarget) {
                    glm::vec3 pos = planetPositions[i];
                    float scale = g_planets[i].size;
                    glUseProgram(colorProg);
                    glm::mat4 latticeModel = glm::mat4(1.0f);
                    latticeModel = glm::translate(latticeModel, pos);
                    latticeModel = glm::rotate(latticeModel, g_planets[i].rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
                    latticeModel = glm::scale(latticeModel, glm::vec3(scale));
                    glm::mat4 latticeMVP = projection * view * latticeModel;
                    glUniformMatrix4fv(glGetUniformLocation(colorProg, "uMVP"), 1, GL_FALSE, glm::value_ptr(latticeMVP));

                    glBindVertexArray(g_latticeVis.pointVAO);
                    glPointSize(8.0f);
                    glDrawArrays(GL_POINTS, 0, g_latticeVis.pointCount);

                    glBindVertexArray(g_latticeVis.lineVAO);
                    glLineWidth(2.0f);
                    glDrawArrays(GL_LINES, 0, g_latticeVis.lineCount);
                    break;
                }
            }
        }

        checkError();
        glfwSwapBuffers(window);
    }

    // cleanup
    glDeleteVertexArrays(1, &sphere.VAO);
    glDeleteBuffers(1, &sphere.VBO);
    glDeleteBuffers(1, &sphere.EBO);
    glDeleteVertexArrays(1, &ffdMesh.VAO);
    glDeleteBuffers(1, &ffdMesh.VBO);
    glDeleteBuffers(1, &ffdMesh.EBO);
    glDeleteVertexArrays(1, &ringMesh.VAO);
    glDeleteBuffers(1, &ringMesh.VBO);
    glDeleteBuffers(1, &ringMesh.EBO);
    glDeleteVertexArrays(1, &starfield.VAO);
    glDeleteBuffers(1, &starfield.VBO);
    glDeleteVertexArrays(1, &g_latticeVis.pointVAO);
    glDeleteBuffers(1, &g_latticeVis.pointVBO);
    glDeleteVertexArrays(1, &g_latticeVis.lineVAO);
    glDeleteBuffers(1, &g_latticeVis.lineVBO);
    glDeleteProgram(planetProg);
    glDeleteProgram(sunProg);
    glDeleteProgram(colorProg);
    for (auto& p : g_planets) glDeleteTextures(1, &p.texture);
    glfwTerminate();
    return 0;
}
