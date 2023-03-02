public class cmovd {

	public static double test(double f) {
		if(f < 10.78) {
			f = 100;
		}
		return f;
	}
	public static void main(String[] args) {
		double f = 5.3;
		f = test(f);
	}
}
