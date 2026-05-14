#include<iostream>
#include<glad/glad.h>
#include<GLFW/glfw3.h>

using namespace std;

void frameBufferSizeCallBack(GLFWwindow* window,int width,int hight){//窗口大小的回调函数
    cout<<"大小改变为："<<width<<"*"<<hight<<endl;
}

void keyCallback(GLFWwindow* window,int key,int scancode,int action,int mod){
    cout<<"键值为"<<":"<<key<<"被"<<(action==1?"按下":"抬起")<<endl;
    if(mod==GLFW_MOD_CONTROL||mod==GLFW_MOD_SHIFT){
        cout<<"按下了"<<(mod==GLFW_MOD_CONTROL?"ctrl":"shift")<<endl;
    }
    // if(key==GLFW_KEY_ESCAPE&&action==GLFW_PRESS){
    //     glfwSetWindowShouldClose(window,true);
    // }
}

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

    //监听窗口大小变化
    glfwSetFramebufferSizeCallback(window,frameBufferSizeCallBack);
    //监听键盘
    glfwSetKeyCallback(window,keyCallback);


    //用GLAD加载opengl函数
    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)){
        cout<<"Failed to initialize GLAD"<<endl;
    }

    //设置视口
    glViewport(0,0,20,90);
    //设置清屏颜色
    glClearColor(0.2f,0.2f,1.0f,1.0f);

    //窗体循环
    while(!glfwWindowShouldClose(window)){
        //检查有没有触发什么事件(比如键盘输入、鼠标移动等)
        glfwPollEvents();
        //交换颜色缓冲
        //glfwSwapBuffers(window);
        //清理画布
        glClear(GL_COLOR_BUFFER_BIT);

        //接下来要渲染，但是还没学


        //切换双缓存
        glfwSwapBuffers(window);
    }

    //相关清理
    glfwTerminate();

    return 0;
}