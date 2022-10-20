## 01_Data Lab

个别地方参考了别人的，因为限制了很多操作符，有些地方就很像脑筋急转弯一样

## 02_Bomb Lab

通过对可执行文件的反汇编来得到让程序正常结束的 6 行字符串，答案在 res.txt 中

### phase_1

phase_1(char* input):
- phase1 直接拿 input 和一个指定的字符串做比较，地址为 0x402400（红色部分）
- 黄色部分查看了 0x402400 处的字符串


![phase-1](./img/phase_1.png)

### phase_2

phase_2(char* input):
- 红色部分在栈中开了一个 4x6字节 的空间（实际开了0x28），并把 栈指针%rsp 作为第二个参数传给了 read_six_numbers，第一个参数还是我们的 input字符串 的指针；read_six_numbers 会把读到的 6 个 int 存放到栈中
- 黄色部分指定了循环的结束位置：0x18(%rsp) 给到 %rbp，从 %rsp 开始正好是 24 字节

![phase-2](./img/phase_2.png)

read_six_numbers(char* input, int* arr):
- 函数利用 sscanf 格式化 input 得到 6 个 int，并存放在 arr 中
- 红色部分是分配 sscanf 的参数，第一个参数(%rdi)还是 input；第二个参数(%rsi)是格式化字符串，地址是 0x4025c3，蓝色部分可以看到确实读了 6 个 int，剩下 6 个参数就是 int*，分别放在 (%rdx, rcx, r8, r9, %rsp, %rsp+4) 中

![read_six_numbers](./img/read_six_numbers.png)


