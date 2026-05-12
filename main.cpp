#include<iostream>
#include<glad/glad.h>
#include<GLFW/glfw3.h>

using namespace std;
int main(){
    //初始化glfw
    glfwInit();
    //设置glfw版本
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,6);
    //启用核心渲染
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);

    //创建窗体对象
    GLFWwindow* window=glfwCreateWindow(1600,1000,"An Empty Window",NULL,NULL);
    //把这个窗口设置为opengl的绘制舞台
    glfwMakeContextCurrent(window);

    //窗体循环
    while(!glfwWindowShouldClose(window)){
        //检查有没有触发什么事件(比如键盘输入、鼠标移动等)
        glfwPollEvents();
        //交换颜色缓冲
        glfwSwapBuffers(window);
    }

    //相关清理
    glfwTerminate();

    return 0;
}