## DEBUG模式

server端的正常情况是不会故意发出错误信息的，为此我们特地引入了debug模式，在server.c的宏定义处有注明。

`#define DEBUG_MODE 1`   

为1时server会有WRONG_PERCENT概率故意发错，检查client的重连

`#define WRONG_PERCENT 1` 

出错的概率1~100
