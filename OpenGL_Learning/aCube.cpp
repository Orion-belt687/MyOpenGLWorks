#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cassert>
#include <iostream>

using namespace std;

const unsigned int SCR_WIDTH = 1600;
const unsigned int SCR_HEIGHT = 1000;

const char* vertexShaderSource = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
out vec3 vColor;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vColor = aColor;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

void checkError() {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        switch (err) {
            case GL_INVALID_ENUM:
                cout << "GL_INVALID_ENUM" << endl;
                break;
            case GL_INVALID_VALUE:
                cout << "GL_INVALID_VALUE" << endl;
                break;
            case GL_INVALID_OPERATION:
                cout << "GL_INVALID_OPERATION" << endl;
                break;
            case GL_OUT_OF_MEMORY:
                cout << "GL_OUT_OF_MEMORY" << endl;
                break;
            default:
                cout << "unknown error: " << err << endl;
        }
        assert(false);
    }
}

int main() {
    cout << "=== Cube Demo ===" << endl;
    cout << "A / S / D : rotate around X / Y / Z axis" << endl;
    cout << "Q / E     : zoom camera in / out" << endl;
    cout << "ESC       : close window" << endl;

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Cube Demo", NULL, NULL);
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        cout << "Failed to initialize GLAD" << endl;
        return -1;
    }

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
    glEnable(GL_DEPTH_TEST);

    // compile shaders
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        cout << "Vertex shader compilation failed:\n" << infoLog << endl;
    }

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        cout << "Fragment shader compilation failed:\n" << infoLog << endl;
    }

    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        cout << "Program linking failed:\n" << infoLog << endl;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // cube vertices: 6 faces x 4 vertices each = 24 vertices
    // each vertex: pos(3) + color(3) = 6 floats
    float vertices[] = {
        // front  (z= 0.5) - red
        -0.5f, -0.5f,  0.5f,  1.0f, 0.2f, 0.2f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.2f, 0.2f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.2f, 0.2f,
        -0.5f,  0.5f,  0.5f,  1.0f, 0.2f, 0.2f,
        // back   (z=-0.5) - green
        -0.5f, -0.5f, -0.5f,  0.2f, 1.0f, 0.2f,
         0.5f, -0.5f, -0.5f,  0.2f, 1.0f, 0.2f,
         0.5f,  0.5f, -0.5f,  0.2f, 1.0f, 0.2f,
        -0.5f,  0.5f, -0.5f,  0.2f, 1.0f, 0.2f,
        // left   (x=-0.5) - blue
        -0.5f, -0.5f, -0.5f,  0.2f, 0.2f, 1.0f,
        -0.5f, -0.5f,  0.5f,  0.2f, 0.2f, 1.0f,
        -0.5f,  0.5f,  0.5f,  0.2f, 0.2f, 1.0f,
        -0.5f,  0.5f, -0.5f,  0.2f, 0.2f, 1.0f,
        // right  (x= 0.5) - yellow
         0.5f, -0.5f, -0.5f,  1.0f, 1.0f, 0.2f,
         0.5f, -0.5f,  0.5f,  1.0f, 1.0f, 0.2f,
         0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 0.2f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 0.2f,
        // top    (y= 0.5) - cyan
        -0.5f,  0.5f, -0.5f,  0.2f, 1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,  0.2f, 1.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  0.2f, 1.0f, 1.0f,
         0.5f,  0.5f, -0.5f,  0.2f, 1.0f, 1.0f,
        // bottom (y=-0.5) - magenta
        -0.5f, -0.5f, -0.5f,  1.0f, 0.2f, 1.0f,
        -0.5f, -0.5f,  0.5f,  1.0f, 0.2f, 1.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.2f, 1.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 0.2f, 1.0f,
    };

    unsigned int indices[] = {
         0,  1,  2,  2,  3,  0,  // front
         4,  5,  6,  6,  7,  4,  // back
         8,  9, 10, 10, 11,  8,  // left
        12, 13, 14, 14, 15, 12,  // right
        16, 17, 18, 18, 19, 16,  // top
        20, 21, 22, 22, 23, 20,  // bottom
    };

    unsigned int VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // position (location=0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // color (location=1)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    float rotX = 0.0f, rotY = 0.5f, rotZ = 0.0f;
    float cameraDist = 3.0f;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // ---- keyboard input ----
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            rotX += 0.02f;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            rotY += 0.02f;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            rotZ += 0.02f;
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            cameraDist -= 0.02f;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
            cameraDist += 0.02f;
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        // clamp camera distance
        if (cameraDist < 1.0f)  cameraDist = 1.0f;
        if (cameraDist > 10.0f) cameraDist = 10.0f;

        // ---- render ----
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(shaderProgram);

        // model: rotate around X, Y, Z
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::rotate(model, rotX, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, rotY, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, rotZ, glm::vec3(0.0f, 0.0f, 1.0f));

        // view: camera looking at origin from z-axis
        glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f, 0.0f, cameraDist),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

        // projection: perspective
        glm::mat4 projection = glm::perspective(
            glm::radians(45.0f),
            (float)SCR_WIDTH / (float)SCR_HEIGHT,
            0.1f,
            100.0f
        );

        glm::mat4 mvp = projection * view * model;
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uMVP"),
                           1, GL_FALSE, glm::value_ptr(mvp));

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        checkError();
        glfwSwapBuffers(window);
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteProgram(shaderProgram);

    glfwTerminate();
    return 0;
}
