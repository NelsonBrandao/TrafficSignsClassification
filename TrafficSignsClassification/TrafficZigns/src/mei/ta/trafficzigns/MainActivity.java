package mei.ta.trafficzigns;

import java.io.File;
import java.io.IOException;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.opencv.android.BaseLoaderCallback;
import org.opencv.android.LoaderCallbackInterface;
import org.opencv.android.OpenCVLoader;
import org.opencv.android.Utils;
import org.opencv.core.Core;
import org.opencv.core.CvType;
import org.opencv.core.Mat;
import org.opencv.core.Point;
import org.opencv.core.Rect;
import org.opencv.core.Scalar;
import org.opencv.highgui.Highgui;
import org.opencv.imgproc.Imgproc;

import android.app.Activity;
import android.app.Dialog;
import android.app.ProgressDialog;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.AsyncTask;
import android.os.Bundle;
import android.os.Environment;
import android.provider.MediaStore;
import android.view.MotionEvent;
import android.view.WindowManager;
import android.widget.ImageView;
import android.widget.Toast;

public class MainActivity extends Activity
{
	private static final int REQUEST_DATABASE_DOWNLOAD = 1;
	private static final int REQUEST_IMAGE_CAPTURE = 2;
	
	private boolean m_isInitialized = false,
			m_isWaitingContinue = false;
	private Mat m_templateTriangle,
			m_templateCircle,
			m_templateSquare,
			m_currentImage;
	private Bitmap m_currentBmp;
	private ProgressDialog m_processing;
	private ImageView m_imageView;
	
	// Native methods _
	public native boolean initTrafficZignsDetector(long triangle, long circle, long square, String modelPath);
	public native String[] locateTrafficZigns(long image);
	
	// _____
    private BaseLoaderCallback mLoaderCallback = new BaseLoaderCallback(this)
    {
        @Override
        public void onManagerConnected(int status)
        {
            switch (status)
            {
                case LoaderCallbackInterface.SUCCESS:
                	if (!m_isInitialized)
                	{
                		System.loadLibrary("traffic_zigns");
                        if (!checkForAvailability("trafficSignz.xml"))
                        	startActivityForResult(new Intent(getApplicationContext(), DownloaderActivity.class), REQUEST_DATABASE_DOWNLOAD);
                        else { initDetection(); initCapture(); }
                        m_isInitialized = true;
                	}
                	break;
                default: finish(); break;
            }
        }
    };
	
	private boolean checkForAvailability(String filename)
	{
		boolean result = false;
		File m_file;
		switch (Environment.getExternalStorageState())
		{
			case Environment.MEDIA_MOUNTED:
			case Environment.MEDIA_MOUNTED_READ_ONLY:
				m_file = new File(Environment.getExternalStorageDirectory(), "/" + filename);
				result = m_file.exists();
				break;
			default: break;
		}
		return result;
	}
	
	private void initDetection()
	{
        try
        {
        	m_templateTriangle = Utils.loadResource(getApplicationContext(),R.drawable.template_triangle,Highgui.CV_LOAD_IMAGE_COLOR);
        	m_templateCircle = Utils.loadResource(getApplicationContext(),R.drawable.template_circle,Highgui.CV_LOAD_IMAGE_COLOR);
        	m_templateSquare = Utils.loadResource(getApplicationContext(),R.drawable.template_square,Highgui.CV_LOAD_IMAGE_COLOR);
        	if (!initTrafficZignsDetector(m_templateTriangle.getNativeObjAddr(),
        			m_templateCircle.getNativeObjAddr(),
        			m_templateSquare.getNativeObjAddr(),
        			Environment.getExternalStorageDirectory() + "/trafficSignz.xml"))
        		finish();
        }
        catch (Exception e) { finish(); }
	}
	
	private void initCapture()
	{
		Intent m_intent;
		Uri imageUri;
		switch (Environment.getExternalStorageState())
		{
			case Environment.MEDIA_MOUNTED:
			case Environment.MEDIA_MOUNTED_READ_ONLY:
				m_intent = new Intent(MediaStore.ACTION_IMAGE_CAPTURE);
				 imageUri = Uri.fromFile(new File(Environment.getExternalStorageDirectory(), "/img.jpg"));
				m_intent.putExtra(MediaStore.EXTRA_OUTPUT, imageUri);
				m_intent.putExtra(MediaStore.EXTRA_SCREEN_ORIENTATION, ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR);
				startActivityForResult(m_intent, REQUEST_IMAGE_CAPTURE);
				break;
			default: finish(); break;
		}
	}
	
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setContentView(R.layout.activity_main);
        m_imageView = (ImageView)findViewById(R.id.viewer);
    }
    
    @Override
    protected void onResume()
    {
    	super.onResume();
    	OpenCVLoader.initAsync(OpenCVLoader.OPENCV_VERSION_2_4_9, this, mLoaderCallback);
    }
    
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data)
    {
    	super.onActivityResult(requestCode, resultCode, data);
    	if (requestCode == REQUEST_DATABASE_DOWNLOAD)
    	{
    		if (resultCode == RESULT_OK)
    		{
    	        if (!checkForAvailability("trafficSignz.xml"))
    	        	startActivityForResult(new Intent(this, DownloaderActivity.class),REQUEST_DATABASE_DOWNLOAD);
    	        else { initDetection(); initCapture(); }
    		}
    		else finish();
    	}
    	
    	if (requestCode == REQUEST_IMAGE_CAPTURE)
    	{
    		if (resultCode == RESULT_OK)
    		{
            	if (checkForAvailability("img.jpg"))
            	{
            		m_currentBmp = BitmapFactory.decodeFile(Environment.getExternalStorageDirectory() + "/img.jpg");
            		m_currentImage = new Mat();
	                Utils.bitmapToMat(m_currentBmp, m_currentImage);
	                m_currentBmp.recycle();
	                
	                new DetectZigns().execute(m_currentImage);
            	}
    		}
    		else finish();
    	}
    }
    
    @Override
    public boolean onTouchEvent(MotionEvent event)
    {
    	if (m_isWaitingContinue)
    	{
    		switch (event.getAction())
    		{
				case MotionEvent.ACTION_DOWN:
					m_imageView.setImageResource(R.color.black);
					initCapture();
					m_currentBmp.recycle();
					m_isWaitingContinue = false;
				break;
			default: break;
			}
    	}
    	return super.onTouchEvent(event);
    }
    
    @Override
    @Deprecated
    protected Dialog onCreateDialog(int id)
    {
	    switch (id)
	    {
	        case 0:
	        	m_processing = new ProgressDialog(this);
	        	m_processing.setMessage("Processing. Please wait...");
	        	m_processing.setIndeterminate(true);
	        	m_processing.setProgressStyle(ProgressDialog.STYLE_SPINNER);
	        	m_processing.setCancelable(false);
	        	m_processing.show();
	        	break;
	    	default: break;
	    }
	    return m_processing;
    }
    
    class DetectZigns extends AsyncTask<Mat, String, String[]>
    {
        @Override
        protected void onPreExecute()
        {
            super.onPreExecute();
            showDialog(0);
        }
        
        @Override
        protected String[] doInBackground(Mat... m)
        {
        	String[] m_result = locateTrafficZigns(m[0].getNativeObjAddr());
        	return m_result;
        }
        
        @Override
        protected void onPostExecute(String[] result)
        {
            dismissDialog(0);
            
            // Regex here, get all results and transform them into relevant information
            Pattern m_pattern = Pattern.compile("(\\d+)@\\((\\d+),(\\d+)\\)-\\((\\d+),(\\d+)\\)");
            for (int i = 0; i < result.length; i++)
            {
                Matcher m =m_pattern.matcher(result[i]);
                if (m.find( ))
                {
                	try
                	{
                    	Rect m_rect = new Rect(new Point(Integer.parseInt(m.group(2)), Integer.parseInt(m.group(3))),
                    			new Point(Integer.parseInt(m.group(4)), Integer.parseInt(m.group(5))));
                    	
                    	Core.rectangle(m_currentImage, m_rect.tl(), m_rect.br(), new Scalar(0,255,0), 5);
                    	
						Mat m_signMat = Utils.loadResource(getApplicationContext(), getResources().getIdentifier("signal_" + m.group(1), "drawable", getPackageName()), Highgui.CV_LOAD_IMAGE_COLOR);
						Imgproc.cvtColor(m_signMat, m_signMat, Imgproc.COLOR_BGRA2RGBA);
						Imgproc.resize(m_signMat,  m_signMat, m_rect.size());
						m_signMat.copyTo(m_currentImage.submat(m_rect));
						m_signMat.release();
					}
                	catch (Exception e) { e.printStackTrace(); }
                }
            }
            m_currentBmp = Bitmap.createBitmap(m_currentImage.cols(), m_currentImage.rows(), Bitmap.Config.ARGB_8888);
            Utils.matToBitmap(m_currentImage, m_currentBmp);
            m_imageView.setImageBitmap(m_currentBmp);
            m_currentImage.release();
            
            m_isWaitingContinue = true;
            
            Toast.makeText(getApplicationContext(), "Touch to continue...", Toast.LENGTH_SHORT).show();
        }
        
        @Override
        protected void onCancelled()
        {
        	super.onCancelled();
        	finish();
        }
    }
}
