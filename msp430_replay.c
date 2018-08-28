#include <msp430.h> 
#include <string.h>

//这个宏仅针对8MHz
#define delay_us(us) __delay_cycles(8*(us))
#define delay_ms(ms) __delay_cycles(8000*(ms))

//信号缓冲区大小
#define signal_frames 5

//每个信号的码数
#define signal_codes_num 32
//真实的signal bit time width   1.333ms/4=333.25us
#define real_signal_bit_width 333
//用于接收信号的signal bit time width  须略低于真实值
#define signal_bit_width 320
//同步位有31位的低bits
#define syn_low_bits_width 31

void gpio_init();
void int_init();
void signal_buffer_init();
void led1_tips(); //信号缓冲区满时提示
void trans_signal();
void dump_signal();

char need_open_dump_signal = 0x00; //由发射模式切换到接收模式的辅助标志位
char door_signal_buffer[signal_frames][signal_codes_num];
char default_door_signal[signal_codes_num] = { '8', '0', '0', '0', '0', '0',
		'0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0',
		'0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0' }; //用于初始化缓冲区
char replay_mode = 0xff; //ff:信号接收模式，00:信号发射模式
int received_signal_frames = 0; //实际接收信号个数

int main(void) {
	WDTCTL = WDTPW | WDTHOLD;	// Stop watchdog timer

	BCSCTL1 = CALBC1_8MHZ;     //设置 DCO 频率为8MHz
	DCOCTL = CALDCO_8MHZ;

	gpio_init();
	int_init();
	signal_buffer_init();

	_EINT(); //开总中断

	while (1) {
		if (!replay_mode) { //发射模式
			trans_signal();
		}
	}
}

#pragma vector = PORT1_VECTOR
__interrupt void PORT_1(void) {
	if ((P1IFG & BIT3) == BIT3) { //按键(P1^3)中断  切换接收/发射信号
		delay_ms(1); //消抖
		if (replay_mode) { //当前为接收模式，改为发射模式
			if (received_signal_frames >= 1) { //至少要采集一个信号才可以发射
				replay_mode = 0x00;
			}
		} else if (!replay_mode) { //当前为发射模式，改为接收模式
			//重置door_signal_buffer
			replay_mode = 0xff;

			need_open_dump_signal = 0xff;			//告诉发射函数结束后把接收中断打开
		}
		P1IFG &= ~BIT3; //中断标志位清零
	} else if ((P1IFG & BIT4) == BIT4 && replay_mode) { //接收信号电平上升沿中断(P1^4)
		dump_signal();
		P1IFG &= ~BIT4; //中断标志位清零
		if (received_signal_frames >= signal_frames) { //超过缓冲区大小则不再接收信号
			P1IE &= ~BIT4; // 关闭P1.4中断
			led1_tips();
		}
	}
}

void gpio_init() {
	//接收信号(P1^0)、发射信号(P1^6) led状态显示
	P1DIR |= (BIT0 + BIT6); //设置P1.0口、P1.6口为输出
	P1OUT &= ~BIT6; //默认关闭发射led显示，P1^6
	P1OUT |= BIT0;

	//接收信号电平(P1^4)
	P1DIR &= ~(BIT4); // P1.4设为输入

	//发射信号电平(P1^5)
	P1DIR |= BIT5;

	//按键(P1^3) 切换接收/发射信号
	P1DIR &= ~(BIT3); // P1.3设为输入
}

void int_init() {
	//按键中断(P1^3)
	P1REN |= BIT3; //启用P1.3内部上下拉电阻
	P1OUT |= BIT3; //将电阻设置为上拉
	P1IES |= BIT3; // P1.3设为下降沿中断
	P1IE |= BIT3; // 允许P1.3中断

	//接收信号电平上升沿中断(P1^4)
	P1IES &= ~BIT4; // P1.4设为上升沿中断
	P1IE |= BIT4; // 允许P1.4中断

	P1IFG &= ~BIT3; //按键中断(P1^3)标志位清零
	P1IFG &= ~BIT4; //接收信号电平下降沿中断(P1^4)标志位清零
}

void signal_buffer_init() {
	int i;
	for (i = 0; i < signal_frames; i++) {
		memcpy(door_signal_buffer[i], default_door_signal, signal_codes_num);
	}
}

void led1_tips() {
	int i = 20, j;
	while (i--) {
		P1OUT ^= BIT0;
		j = 200;
		while (j--) {
			delay_ms(1);
		}
	}

	P1OUT |= BIT0; //确保led1是亮着的
}

void trans_0() {

	P1OUT &= ~BIT5;
	delay_us(real_signal_bit_width);
}

void trans_1() {

	P1OUT |= BIT5;
	delay_us(real_signal_bit_width);
}

void trans_signal() {
	int i, j;

	P1OUT &= ~BIT0; //发射信号时关闭接收状态显示灯

	for (i = 0; i < received_signal_frames; i++) { //发射信号的个数以实际捕获的为准
		int replay_num = 5;
		while (replay_num--) { //每个信号重放5次
			for (j = 0; j < signal_codes_num; j++) {
				P1OUT ^= BIT6; //发射状态指示灯
				switch (door_signal_buffer[i][j]) {
				case '0':
					trans_0();
					trans_0();
					trans_0();
					trans_0();
					break;
				case '8':
					trans_1();
					trans_0();
					trans_0();
					trans_0();
					break;
				case 'E':
					trans_1();
					trans_1();
					trans_1();
					trans_0();
					break;
				}
			}
		}
	}

	if (need_open_dump_signal) {
		need_open_dump_signal = 0x00;

		received_signal_frames = 0;
		signal_buffer_init();
		P1IE |= BIT4; // 打开P1.4中断
	}
}

void dump_signal() {
	P1OUT &= ~BIT6; //接收时关闭发射状态显示灯

	while ((P1IN & BIT4))
		;
	/******************判断同步位起始位后的n个低电平位*******************/
	int i;
	for (i = 0; i < syn_low_bits_width; i++) {
		P1OUT ^= BIT0; //接收状态指示灯

		delay_us(signal_bit_width);
		if ((P1IN & BIT4)) {	//若P1^4为高电平则退出中断
			//Uart0SendVoltInfo(i);
			return;
		}
	}
	while ((P1IN & BIT4) == 0)
		;
	/******************接收数据*******************/
	for (i = 0; i < 24; i++) {
		P1OUT ^= BIT0;			//接收状态指示灯

		delay_us(signal_bit_width*2);
		if ((P1IN & BIT4)) {	//若P1^4为高电平则数据为E
			door_signal_buffer[received_signal_frames][i + 8] = 'E';
			while ((P1IN & BIT4))
				;
			while ((P1IN & BIT4) == 0)
				;
		} else if ((P1IN & BIT4) == 0) {	//若P1^4为低电平则数据为8
			door_signal_buffer[received_signal_frames][i + 8] = '8';
			while ((P1IN & BIT4) == 0)
				;
		}
	}

	//处理冗余数据
//	for(i=0;i<received_signal_frames;i++){
//		if(!memcmp(door_signal_buffer[received_signal_frames],door_signal_buffer[i],signal_codes_num)){
//			memcpy(door_signal_buffer[received_signal_frames], default_door_signal,signal_codes_num);
//			return;
//		}
//	}

	received_signal_frames++;
}
