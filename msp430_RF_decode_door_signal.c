#include <msp430.h>
#include <string.h>

//这个宏仅针对8MHz
#define delay_us(us) __delay_cycles(8*(us))
#define delay_ms(ms) __delay_cycles(8000*(ms))

//实测one signale bit占用1.333ms/4，即0.33325ms，所以把signal_bit_width定为300，略小于实际值，便于进行信号码位判断
#define signal_bit_width 300
#define syn_low_bits_width 31

const char default_door_signal[34] = { '8', '0', '0', '0', '0', '0', '0', '0',
		'0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0',
		'0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '\n', '\0' }; //多了两位，便于串口发送、打印
char open_door_signal[34];
int i = 0;


void Uart0SendString(char *s) {
	while (*s != '\0') {
		UCA0TXBUF = *s;
		while ((IFG2 & UCA0TXIFG) == 0)
			; //查询发送是否结束
		IFG2 &= ~UCA0TXIFG; //清除发送一标志位
		s++;
	}
}

void Uart0SendVoltInfo(int num) {
	char s[7];
	s[0] = ' ';
	s[1] = num / 10000 % 10 + 48;
	s[2] = num / 1000 % 10 + 48;
	s[3] = num / 100 % 10 + 48;
	s[4] = num / 10 % 10 + 48;
	s[5] = num % 10 + 48;
	s[6] = '\0';
	Uart0SendString(s);
}

int main(void) {
	// Stop watchdog timer to prevent time out reset
	WDTCTL = WDTPW + WDTHOLD;

	UCA0CTL1 |= UCSWRST;      // USCI_A0 进入软件复位状态
	UCA0CTL1 |= UCSSEL_2;   //时钟源选择 SMCLK
	BCSCTL1 = CALBC1_8MHZ;     //设置 DCO 频率为8MHz
	DCOCTL = CALDCO_8MHZ;
	P1SEL = BIT1 + BIT2;    // P1.1 = RXD, P1.2=TXD
	P1SEL2 = BIT1 + BIT2;      // P1.1 = RXD, P1.2=TXD
	UCA0BR0 = 0x41;    //时钟源 8MHz 时波特率为9600
	UCA0BR1 = 0x03;      //时钟源 8MHz 时波特率为9600
	UCA0MCTL = 0x00;
	UCA0CTL1 &= ~UCSWRST;   //初始化 USCI_A0 状态机
	IE2 |= UCA0RXIE;    //使能 USCI_A0 接收中断

	P1DIR |= BIT0; //设置P1.0口为输出
	P1DIR &= ~(BIT3); // P1.3设为输入
	P1IE |= BIT3; //使能P1.3中断
	P1IES &= ~BIT3; //P1.3口上升沿触发中断
	P1IFG &= ~BIT3; //中断标志位清零

	_EINT(); //开总中断

	memcpy(open_door_signal, default_door_signal, 34);

	while (1)
		;
}

/*中断服务程序*/
#pragma vector = PORT1_VECTOR
__interrupt void PORT_1(void) { //发生了上升沿中断

	/******************找同步位*******************/
	//判断同步位的起始位 <-- 不能正常判断起始位，干脆注释掉，毕竟这一步比较鸡肋
//	for (i = 0; i < signal_bit_width; i++) {
//		P1OUT ^= BIT0;
//		delay_us(1);
//		if ((P1IN & BIT3) == 0) {	//若P1^3为低电平则退出中断
//			//Uart0SendVoltInfo(i);
//			goto exit_interrupt;
//		}
//	}
	while ((P1IN & BIT3))
		;
	/******************判断同步位起始位后的n个低电平位*******************/
	for (i = 0; i < syn_low_bits_width; i++) {
		P1OUT ^= BIT0;
		delay_us(signal_bit_width);
		if ((P1IN & BIT3)) {	//若P1^3为高电平则退出中断
			//Uart0SendVoltInfo(i);
			goto exit_interrupt;
		}
	}
	while ((P1IN & BIT3) == 0)
		;
	/******************接收数据*******************/
	for (i = 0; i < 24; i++) {
		delay_us(signal_bit_width*2);
		if ((P1IN & BIT3)) {	//若P1^3为高电平则数据为E
			open_door_signal[i + 8] = 'E';
			while ((P1IN & BIT3))
				;
			while ((P1IN & BIT3) == 0)
				;
		} else if ((P1IN & BIT3) == 0) {	//若P1^3为低电平则数据为8
			open_door_signal[i + 8] = '8';
			while ((P1IN & BIT3) == 0)
				;
		}
	}

	Uart0SendString(open_door_signal);
	memcpy(open_door_signal, default_door_signal, 34);
	exit_interrupt: P1IFG &= ~BIT3; //中断标志位清零
}
