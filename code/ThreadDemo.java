
import java.lang.*;

public class ThreadDemo implements Runnable {

   Thread t;
   ThreadDemo() {
    
      // thread created
      t = new Thread(this, "Admin Thread");
     
      // prints thread created
      System.out.println("thread  = " + t);
      
      // this will call run() function
      System.out.println("Calling run() function... ");
      t.start();
   }

   public void run() {
	   int i = 0;
	   while(i < 1000)
      System.out.println("Inside run()function");
   }

   public static void main(String args[]) {
      new ThreadDemo();
   }
} 
