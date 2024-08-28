
void print(char *str);
void putc(char c);

void main()
{
	print("Hi Sid, You can do it!!");
}

void putc(char c)
{
	int mmio = 0x10000000;
	*((char*)mmio) = c;
}

void print(char *str)
{
	int i = 0;
	while (str[i] != '\0') {
		putc(str[i]);
		i++;
	}
}
