#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cassert>
#include <iostream>
#include <vector>

using namespace std;

const unsigned int SCR_WIDTH = 1600;
const unsigned int SCR_HEIGHT = 1000;

const char* vertexShaderSource = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aColor;
out vec3 vColor;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
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

//  B(t) = (1-t)^3*P0 + 3(1-t)^2*t*P1 + 3(1-t)*t^2*P2 + t^3*P3
vector<float> computeBezierPoints(const float ctrlPoints[4][2], int numSamples) {
    vector<float> points;
    for (int i = 0; i <= numSamples; i++) {
        float t = (float)i / numSamples;
        float t1 = 1.0f - t;
        float t1_2 = t1 * t1;
        float t1_3 = t1_2 * t1;
        float t2 = t * t;
        float t3 = t2 * t;

        float x = t1_3 * ctrlPoints[0][0] +
                  3.0f * t1_2 * t * ctrlPoints[1][0] +
                  3.0f * t1 * t2 * ctrlPoints[2][0] +
                  t3 * ctrlPoints[3][0];
        float y = t1_3 * ctrlPoints[0][1] +
                  3.0f * t1_2 * t * ctrlPoints[1][1] +
                  3.0f * t1 * t2 * ctrlPoints[2][1] +
                  t3 * ctrlPoints[3][1];

        points.push_back(x);
        points.push_back(y);
    }
    return points;
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Cubic Bezier Curve Demo", NULL, NULL);
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        cout << "Failed to initialize GLAD" << endl;
        return -1;
    }

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    //  4     NDC  [-1, 1]
    float ctrlPoints[4][2] = {
        {-0.8f, -0.5f},  // P0
        {-0.3f,  0.8f},  // P1
        { 0.3f, -0.7f},  // P2
        { 0.8f,  0.4f}   // P3
    };

    const int NUM_SAMPLES = 200;
    vector<float> curvePoints = computeBezierPoints(ctrlPoints, NUM_SAMPLES);

    // --- curve VAO/VBO ---
    unsigned int curveVAO, curveVBO;
    glGenVertexArrays(1, &curveVAO);
    glGenBuffers(1, &curveVBO);

    vector<float> curveVertices;
    for (size_t i = 0; i < curvePoints.size(); i += 2) {
        curveVertices.push_back(curvePoints[i]);      // x
        curveVertices.push_back(curvePoints[i + 1]);  // y
        curveVertices.push_back(1.0f);                 // r
        curveVertices.push_back(0.84f);                // g
        curveVertices.push_back(0.0f);                 // b
    }

    glBindVertexArray(curveVAO);
    glBindBuffer(GL_ARRAY_BUFFER, curveVBO);
    glBufferData(GL_ARRAY_BUFFER, curveVertices.size() * sizeof(float), curveVertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // --- control polygon VAO/VBO ---
    unsigned int polyVAO, polyVBO;
    glGenVertexArrays(1, &polyVAO);
    glGenBuffers(1, &polyVBO);

    float polyVertices[] = {
        ctrlPoints[0][0], ctrlPoints[0][1], 0.4f, 0.4f, 0.4f,
        ctrlPoints[1][0], ctrlPoints[1][1], 0.4f, 0.4f, 0.4f,
        ctrlPoints[2][0], ctrlPoints[2][1], 0.4f, 0.4f, 0.4f,
        ctrlPoints[3][0], ctrlPoints[3][1], 0.4f, 0.4f, 0.4f,
    };

    glBindVertexArray(polyVAO);
    glBindBuffer(GL_ARRAY_BUFFER, polyVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(polyVertices), polyVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // --- control points VAO/VBO ---
    unsigned int ctrlVAO, ctrlVBO;
    glGenVertexArrays(1, &ctrlVAO);
    glGenBuffers(1, &ctrlVBO);

    float ctrlPointVertices[] = {
        ctrlPoints[0][0], ctrlPoints[0][1], 1.0f, 1.0f, 1.0f,  // P0 white
        ctrlPoints[1][0], ctrlPoints[1][1], 0.0f, 1.0f, 1.0f,  // P1 cyan
        ctrlPoints[2][0], ctrlPoints[2][1], 1.0f, 0.0f, 1.0f,  // P2 magenta
        ctrlPoints[3][0], ctrlPoints[3][1], 1.0f, 1.0f, 0.0f,  // P3 yellow
    };

    glBindVertexArray(ctrlVAO);
    glBindBuffer(GL_ARRAY_BUFFER, ctrlVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(ctrlPointVertices), ctrlPointVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glEnable(GL_PROGRAM_POINT_SIZE);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(shaderProgram);

        // draw control polygon
        glBindVertexArray(polyVAO);
        glDrawArrays(GL_LINE_STRIP, 0, 4);

        // draw Bezier curve
        glBindVertexArray(curveVAO);
        glDrawArrays(GL_LINE_STRIP, 0, NUM_SAMPLES + 1);

        // draw control points
        glBindVertexArray(ctrlVAO);
        glPointSize(12.0f);
        glDrawArrays(GL_POINTS, 0, 4);

        checkError();
        glfwSwapBuffers(window);
    }

    glDeleteVertexArrays(1, &curveVAO);
    glDeleteVertexArrays(1, &polyVAO);
    glDeleteVertexArrays(1, &ctrlVAO);
    glDeleteBuffers(1, &curveVBO);
    glDeleteBuffers(1, &polyVBO);
    glDeleteBuffers(1, &ctrlVBO);
    glDeleteProgram(shaderProgram);

    glfwTerminate();
    return 0;
}
