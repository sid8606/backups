import java.lang.Math;

class Main {
  public static void main(String[] args) {

    // create variable in Degree
    double a1 = 268435456.0;  //1<<28
    double a = 1329227995784915872903807060280344576.0;  //1<<120
    double b =   1766847064778384329583297500742918515827483896875618958121606201292619776.0;  //1 << 240
    

   
    // print the sine value
     System.out.println(Math.sin(a1)); // -0.16556897949057876
    System.out.println(Math.sin(a));   // 0.37782010936075202
    System.out.println(Math.sin(b));   // -0.35197227524865778

    // sin() with 0 as its argument
    System.out.println(Math.sin(0.0));  // 0.0
  }
}
