package mei.ta.trafficzigns;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.URL;
import java.net.URLConnection;

import javax.net.ssl.HttpsURLConnection;

import android.app.Activity;
import android.app.Dialog;
import android.app.ProgressDialog;
import android.os.AsyncTask;
import android.os.Bundle;
import android.os.Environment;
import android.view.View;
import android.widget.Button;

public class DownloaderActivity extends Activity
{
	private ProgressDialog m_progressDialog;
	
	@Override
	protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);
		this.setContentView(R.layout.activity_downloader);
		
		Button m_downloadButton = (Button)findViewById(R.id.downloadButton);
		m_downloadButton.setOnClickListener(new View.OnClickListener()
		{
			@Override
			public void onClick(View v)
			{
				new DownloadData().execute("https://raw.githubusercontent.com/NelsonBrandao/TrafficSignsClassification/master/trafficSignz.xml");
			}
		});
		
		Button m_cancelButton = (Button)findViewById(R.id.cancelButton);
		m_cancelButton.setOnClickListener(new View.OnClickListener()
		{
			@Override
			public void onClick(View v)
			{
				setResult(RESULT_CANCELED);
				finish();
			}
		});
	}
	
	@Override
	@Deprecated
	protected Dialog onCreateDialog(int id)
	{
        switch (id)
        {
	        case 0:
	        	m_progressDialog = new ProgressDialog(this);
	        	m_progressDialog.setMessage("Downloading data. Please wait...");
	        	m_progressDialog.setIndeterminate(false);
	        	m_progressDialog.setMax(100);
	        	m_progressDialog.setProgressStyle(ProgressDialog.STYLE_HORIZONTAL);
	        	m_progressDialog.setCancelable(true);
	        	m_progressDialog.show();
	        	break;
	    	default: break;
        }
        return m_progressDialog;
	}
	
    class DownloadData extends AsyncTask<String, String, String>
    {
        @Override
        protected void onPreExecute()
        {
            super.onPreExecute();
            showDialog(0);
        }
        
        @Override
        protected String doInBackground(String... f_url)
        {
            int count;
            try
            {
                URL url = new URL(f_url[0]);
                URLConnection connection = url.openConnection();
//                HttpsURLConnection connection2 = url.op
                connection.connect();
                
                int lenghtOfFile = connection.getContentLength();
                if (lenghtOfFile > 0)
                {
                    InputStream input = new BufferedInputStream(url.openStream(), 8192);
                	OutputStream output = new FileOutputStream(Environment.getExternalStorageDirectory() + "/trafficSignz_temp.xml");
                	
                    byte data[] = new byte[1024];
                    long total = 0;
                    while ((count = input.read(data)) != -1)
                    {
                        total += count;
                        publishProgress(""+(int)((total*100)/lenghtOfFile));
                        output.write(data, 0, count);
                    }
                    output.flush();
                    
                    output.close();
                    input.close();
                }
                else cancel(true);
            }
            catch (Exception e)
            {
            	cancel(true);
            }
            return null;
        }
        
        protected void onProgressUpdate(String... progress)
        {
        	m_progressDialog.setProgress(Integer.parseInt(progress[0]));
       }
        
        @Override
        protected void onPostExecute(String file_url)
        {
            dismissDialog(0);
            
            File m_fileIn, m_fileOut;
    		switch (Environment.getExternalStorageState())
    		{
    			case Environment.MEDIA_MOUNTED:
    			case Environment.MEDIA_MOUNTED_READ_ONLY:
    				m_fileIn = new File(Environment.getExternalStorageDirectory(), "/trafficSignz_temp.xml");
    				m_fileOut = new File(Environment.getExternalStorageDirectory(), "/trafficSignz.xml");
    				if (m_fileIn.exists())
    					if (m_fileIn.renameTo(m_fileOut))
    						setResult(RESULT_OK);
    				break;
    		}
            finish();
        }
        
        @Override
        protected void onCancelled()
        {
        	super.onCancelled();
			setResult(RESULT_CANCELED);
			finish();
        }
    }
}
