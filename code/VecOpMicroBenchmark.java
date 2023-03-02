/**
 * Run with this command to show native assembly:<br/>
 * java -XX:+UnlockDiagnosticVMOptions
 * -XX:CompileCommand=print,VecOpMicroBenchmark.profile VecOpMicroBenchmark
 */
public class VecOpMicroBenchmark {

	private static final int LENGTH = 1024;

	private static long profile(float[] x, float[] y) {
		long t = System.nanoTime();

		for (int i = 0; i < LENGTH; i++) {
			y[i] = y[i] + x[i]; // line 14
		}

		t = System.nanoTime() - t;

		return t;
	}

	public static void main(String[] args) throws Exception {
		float[] x = new float[LENGTH];
		float[] y = new float[LENGTH];

		// to let the JIT compiler do its work, repeatedly invoke
		// the method under test and then do a little nap
		long minDuration = Long.MAX_VALUE;
		for (int i = 0; i < 1000000; i++) {
			long duration = profile(x, y);
			minDuration = Math.min(minDuration, duration);
		}
		Thread.sleep(10);

		System.out.println("\n\nduration: " + minDuration + "ns");
	}
}
