package com.introlab.rtabmap;

import java.io.File;
import java.io.FilenameFilter;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.ProgressDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.PackageManager.NameNotFoundException;
import android.graphics.Point;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Debug;
import android.text.Editable;
import android.text.InputType;
import android.util.Log;
import android.view.Display;
import android.view.Menu;
import android.view.MenuItem;
import android.view.MenuInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import com.google.atap.tangoservice.Tango;

// The main activity of the application. This activity shows debug information
// and a glSurfaceView that renders graphic content.
public class RTABMapActivity extends Activity implements OnClickListener {

  // Tag for debug logging.
  private static final String TAG = RTABMapActivity.class.getSimpleName();

  // The minimum Tango Core version required from this application.
  private static final int  MIN_TANGO_CORE_VERSION = 6804;

  // The package name of Tang Core, used for checking minimum Tango Core version.
  private static final String TANGO_PACKAGE_NAME = "com.projecttango.tango";
  
  public static final String EXTRA_KEY_PERMISSIONTYPE = "PERMISSIONTYPE";
  public static final String EXTRA_VALUE_ADF = "ADF_LOAD_SAVE_PERMISSION";

  // GLSurfaceView and renderer, all of the graphic content is rendered
  // through OpenGL ES 2.0 in native code.
  private Renderer mRenderer;
  private GLSurfaceView mGLView;
  
  ProgressDialog mProgressDialog;

  // Screen size for normalizing the touch input for orbiting the render camera.
  private Point mScreenSize = new Point();

  private MenuItem mItemPause;
  private MenuItem mItemSave;
  private MenuItem mItemOpen;
  private MenuItem mItemExport;
  private MenuItem mItemLocalizationMode;
  private MenuItem mItemTrajectoryMode;
  
  private String mOpenedDatabasePath = "";
  private String mTempDatabasePath = "";
  private String mNewDatabasePath = "";
  private String mWorkingDirectory = "";
  
  private LinearLayout mLayoutDebug;
  
  private int mTotalLoopClosures = 0;
  
  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setTitle(R.string.menu_name);

    // Query screen size, the screen size is used for computing the normalized
    // touch point.
    Display display = getWindowManager().getDefaultDisplay();
    display.getSize(mScreenSize);

    // Setting content view of this activity.
    setContentView(R.layout.activity_rtabmap);

    // Buttons for selecting camera view and Set up button click listeners.
    findViewById(R.id.first_person_button).setOnClickListener(this);
    findViewById(R.id.third_person_button).setOnClickListener(this);
    findViewById(R.id.top_down_button).setOnClickListener(this);

    // OpenGL view where all of the graphics are drawn.
    mGLView = (GLSurfaceView) findViewById(R.id.gl_surface_view);
    
    // Configure OpenGL renderer
    mGLView.setEGLContextClientVersion(2);

    // Configure the OpenGL renderer.
    mRenderer = new Renderer();
    mGLView.setRenderer(mRenderer);
    
    mLayoutDebug = (LinearLayout) findViewById(R.id.debug_layout);
    mLayoutDebug.setVisibility(LinearLayout.GONE);
    
    mProgressDialog = new ProgressDialog(this);
    mRenderer.setProgressDialog(mProgressDialog);

    // Check if the Tango Core is out dated.
    if (!CheckTangoCoreVersion(MIN_TANGO_CORE_VERSION)) {
      Toast.makeText(this, "Tango Core out dated, please update in Play Store", Toast.LENGTH_LONG).show();
      finish();
      return;
    }   
    
    mOpenedDatabasePath = "";
    mTempDatabasePath = "";
    mNewDatabasePath = "";
    mWorkingDirectory = "";
    mTotalLoopClosures = 0;
    
    if(Environment.getExternalStorageState().compareTo(Environment.MEDIA_MOUNTED)==0)
    {
    	File extStore = Environment.getExternalStorageDirectory();
    	mWorkingDirectory = extStore.getAbsolutePath() + "/" + getString(R.string.app_name) + "/";
    	extStore = new File(mWorkingDirectory);
    	extStore.mkdirs();
    	mTempDatabasePath = mWorkingDirectory + "rtabmap.tmp.db";
    	extStore = new File(mTempDatabasePath);
    	
    	if(extStore.exists())
    	{
    		extStore.delete();
    	}   	
    }
    else
    {
    	// show warning that data cannot be saved!
		Toast.makeText(getApplicationContext(), 
				String.format("Failed to get external storage path (SD-CARD, state=%s). Saving disabled.", 
						Environment.getExternalStorageState()), Toast.LENGTH_LONG).show();
    }
    
    RTABMapLib.initialize(this);
    RTABMapLib.openDatabase(mTempDatabasePath);
  }
  
  @Override
  protected void onActivityResult(int requestCode, int resultCode, Intent data) {
  	// Check which request we're responding to
  	if (requestCode == Tango.TANGO_INTENT_ACTIVITYCODE) {
  		// Make sure the request was successful
  		if (resultCode == RESULT_CANCELED) {
  			Toast.makeText(this, "Motion Tracking Permissions Required!",
  					Toast.LENGTH_SHORT).show();
  			finish();
  		}
  	}
  }

  @Override
  protected void onResume() {
    super.onResume();
    
    Log.i(TAG, String.format("onResume()"));
    
    if (Tango.hasPermission(this, Tango.PERMISSIONTYPE_MOTION_TRACKING)) {

    	mGLView.onResume();
        
        mTotalLoopClosures = 0;
        if(mItemOpen != null)
        {
    	    mItemOpen.setEnabled(false);
    	    mItemPause.setChecked(false);
    	    mItemSave.setEnabled(false);
    		mItemExport.setEnabled(false);
        }

        if(RTABMapLib.onResume()!=0)
        {
        	Toast.makeText(getApplicationContext(), 
    				String.format("Failed to connect with Tango!"), Toast.LENGTH_SHORT).show();
        }

    } else {
    	Log.i(TAG, String.format("Asking for motion tracking permission"));
        startActivityForResult(
                Tango.getRequestPermissionIntent(Tango.PERMISSIONTYPE_MOTION_TRACKING),
                Tango.TANGO_INTENT_ACTIVITYCODE);
    }
  }

  @Override
  protected void onPause() {
    super.onPause();
    mGLView.onPause();
    
    // Delete all the non-OpenGl resources.
    RTABMapLib.onPause();
    mOpenedDatabasePath = "";
    RTABMapLib.openDatabase(mTempDatabasePath);
  }

  @Override
  public void onClick(View v) {
    // Handle button clicks.
    switch (v.getId()) {
    case R.id.first_person_button:
      RTABMapLib.setCamera(0);
      break;
    case R.id.third_person_button:
      RTABMapLib.setCamera(1);
      break;
    case R.id.top_down_button:
      RTABMapLib.setCamera(2);
      break;
    default:
      return;
    }
  }

  @Override
  public boolean onTouchEvent(MotionEvent event) {
    // Pass the touch event to the native layer for camera control.
    // Single touch to rotate the camera around the device.
    // Two fingers to zoom in and out.
    int pointCount = event.getPointerCount();
    if (pointCount == 1) {
      float normalizedX = event.getX(0) / mScreenSize.x;
      float normalizedY = event.getY(0) / mScreenSize.y;
      RTABMapLib.onTouchEvent(1, 
          event.getActionMasked(), normalizedX, normalizedY, 0.0f, 0.0f);
    }
    if (pointCount == 2) {
      if (event.getActionMasked() == MotionEvent.ACTION_POINTER_UP) {
        int index = event.getActionIndex() == 0 ? 1 : 0;
        float normalizedX = event.getX(index) / mScreenSize.x;
        float normalizedY = event.getY(index) / mScreenSize.y;
        RTABMapLib.onTouchEvent(1, 
          MotionEvent.ACTION_DOWN, normalizedX, normalizedY, 0.0f, 0.0f);
      } else {
        float normalizedX0 = event.getX(0) / mScreenSize.x;
        float normalizedY0 = event.getY(0) / mScreenSize.y;
        float normalizedX1 = event.getX(1) / mScreenSize.x;
        float normalizedY1 = event.getY(1) / mScreenSize.y;
        RTABMapLib.onTouchEvent(2, event.getActionMasked(),
            normalizedX0, normalizedY0, normalizedX1, normalizedY1);
      }
    }
    return true;
  }
  
  @Override
  public boolean onCreateOptionsMenu(Menu menu) {
	  Log.i(TAG, "called onCreateOptionsMenu;");
	  
	  MenuInflater inflater = getMenuInflater();
	  inflater.inflate(R.menu.optionmenu, menu);
	  
	  mItemPause = menu.findItem(R.id.pause);
	  mItemSave = menu.findItem(R.id.save);
	  mItemOpen = menu.findItem(R.id.open);
	  mItemExport = menu.findItem(R.id.export);
	  mItemLocalizationMode = menu.findItem(R.id.localization_mode);
	  mItemTrajectoryMode = menu.findItem(R.id.trajectory_mode);
	  mItemSave.setEnabled(false);
	  mItemExport.setEnabled(false);
	  mItemOpen.setEnabled(false);
	  
	  return true;
  }
  
  private void updateStatsUI(
		  int nodes, 
		  int words, 
		  int points, 
		  int polygons,
		  float updateTime, 
		  int loopClosureId,
		  int databaseMemoryUsed,
		  int inliers,
		  int featuresExtracted,
		  float hypothesis,
		  int nodesDrawn)
  {
	  if(mItemPause!=null)
	  {
		  ((TextView)findViewById(R.id.status)).setText(mItemPause.isChecked()?"Paused":mItemLocalizationMode.isChecked()?"Localization":"Mapping");
	  }
	  
	  ((TextView)findViewById(R.id.points)).setText(String.valueOf(points));
	  ((TextView)findViewById(R.id.polygons)).setText(String.valueOf(polygons));
	  ((TextView)findViewById(R.id.nodes)).setText(String.format("%d (%d shown)", nodes, nodesDrawn));
	  ((TextView)findViewById(R.id.words)).setText(String.valueOf(words));
	  ((TextView)findViewById(R.id.memory)).setText(String.valueOf(Debug.getNativeHeapAllocatedSize()/(1024*1024)));
	  ((TextView)findViewById(R.id.db_size)).setText(String.valueOf(databaseMemoryUsed));
	  ((TextView)findViewById(R.id.inliers)).setText(String.valueOf(inliers));
	  ((TextView)findViewById(R.id.features)).setText(String.valueOf(featuresExtracted));
	  ((TextView)findViewById(R.id.update_time)).setText(String.format("%.3f", updateTime));
	  ((TextView)findViewById(R.id.hypothesis)).setText(String.format("%.3f (%d)", hypothesis, loopClosureId));
	  if(loopClosureId > 0)
	  {
		  ++mTotalLoopClosures;
		  ((TextView)findViewById(R.id.total_loop)).setText(String.valueOf(mTotalLoopClosures));
		  Toast.makeText(this, "Loop closure detected!", Toast.LENGTH_SHORT).show();
	  }
  }
  
  // called from jni
  public void updateStatsCallback(
		  final int nodes, 
		  final int words, 
		  final int points, 
		  final int polygons,
		  final float updateTime, 
		  final int loopClosureId,
		  final int databaseMemoryUsed,
		  final int inliers,
		  final int features,
		  final float hypothesis,
		  final int nodesDrawn)
  {
	  Log.i(TAG, String.format("updateStatsCallback()"));

	  runOnUiThread(new Runnable() {
		    public void run() {
		    	updateStatsUI(nodes, words, points, polygons, updateTime, loopClosureId, databaseMemoryUsed, inliers, features, hypothesis, nodesDrawn);
		    } 
		});
  }
  
  private void rtabmapInitEventUI(
		  int status, 
		  String msg)
  {
	  Log.i(TAG, String.format("rtabmapInitEventsUI() status=%d msg=%s", status, msg));
	  
	  ((TextView)findViewById(R.id.status)).setText(
			  status == 1 && msg.isEmpty()?mItemPause!=null&&mItemPause.isChecked()?"Paused":mItemLocalizationMode!=null&&mItemLocalizationMode.isChecked()?"Localization":"Mapping":msg);
	  
	  /*0=kInitializing,
		1=kInitialized,
		2=kClosing,
		3=kClosed,
		4=kInfo,
		5=kError*/
	  
		if(status == 3)
		{
			msg = "";
			if(!mNewDatabasePath.isEmpty())
			{
				boolean removed = true;
				File outputFile = new File(mNewDatabasePath);
				if(outputFile.exists())
				{
					removed = outputFile.delete();
				}
				if(removed)
				{
					File tempFile = new File(mTempDatabasePath);
					if(tempFile.renameTo(outputFile))
					{
						msg = String.format("Database saved to \"%s\".", mNewDatabasePath);
						
						Intent intent = new Intent(this, RTABMapActivity.class);
						// use System.currentTimeMillis() to have a unique ID for the pending intent
						PendingIntent pIntent = PendingIntent.getActivity(this, (int) System.currentTimeMillis(), intent, 0);

						// build notification
						// the addAction re-use the same intent to keep the example short
						Notification n  = new Notification.Builder(this)
						        .setContentTitle(getString(R.string.app_name))
						        .setContentText(mNewDatabasePath + " saved!")
						        .setSmallIcon(R.drawable.ic_launcher)
						        .setContentIntent(pIntent)
						        .setAutoCancel(true).build();
						    
						  
						NotificationManager notificationManager = 
						  (NotificationManager) getSystemService(NOTIFICATION_SERVICE);

						notificationManager.notify(0, n); 
					}
					else
					{
						msg = String.format("Failed to rename temporary database from \"%s\" to \"%s\".",
								mTempDatabasePath, mNewDatabasePath);
					}
				}
				else
				{
					msg = String.format("Failed to overwrite the database \"%s\". The temporary database is still correctly saved at \"%s\".",
							mNewDatabasePath, mTempDatabasePath);
				}
			}
			else if(!mOpenedDatabasePath.isEmpty())
			{
				msg = String.format("Database \"%s\" updated.", mOpenedDatabasePath);
			}

			if(!msg.isEmpty())
			{
				Toast.makeText(this, msg, Toast.LENGTH_LONG).show();
			}
			
			mOpenedDatabasePath = "";
			mNewDatabasePath = "";
			
			//restart a new scan by default
			RTABMapLib.openDatabase(mTempDatabasePath);
			
			((TextView)findViewById(R.id.points)).setText(String.valueOf(0));
			((TextView)findViewById(R.id.polygons)).setText(String.valueOf(0));
			((TextView)findViewById(R.id.nodes)).setText(String.valueOf(0));
			((TextView)findViewById(R.id.words)).setText(String.valueOf(0));
			((TextView)findViewById(R.id.memory)).setText(String.valueOf(Debug.getNativeHeapAllocatedSize()/(1024*1024)));
			((TextView)findViewById(R.id.db_size)).setText(String.valueOf(0));
			((TextView)findViewById(R.id.inliers)).setText(String.valueOf(0));
			((TextView)findViewById(R.id.features)).setText(String.valueOf(0));
			((TextView)findViewById(R.id.update_time)).setText(String.valueOf(0));
			((TextView)findViewById(R.id.hypothesis)).setText(String.valueOf(0));
			mTotalLoopClosures = 0;
			((TextView)findViewById(R.id.total_loop)).setText(String.valueOf(mTotalLoopClosures));
			
			if(mItemSave!=null)
			{
				if(mItemPause.isChecked())
				{
					mItemPause.setChecked(false);
					mItemOpen.setEnabled(false);
					mItemSave.setEnabled(false);
					mItemExport.setEnabled(false);
					RTABMapLib.setPausedMapping(false); // resume mapping
				}
			}
			mProgressDialog.dismiss();
		}
  }
  
  //called from jni
  public void rtabmapInitEventCallback(
		  final int status, 
		  final String msg)
  {
	  Log.i(TAG, String.format("rtabmapInitEventCallback()"));

	  runOnUiThread(new Runnable() {
		    public void run() {
		    	rtabmapInitEventUI(status, msg);
		    } 
		});
  }
  
  private void tangoEventUI(
		  int type, 
		  String key,
		  String value)
  {
	  /**
	   * 
	   * "TangoServiceException:X" - The service has encountered an exception, and a text description is given in X.
	   * "FisheyeOverExposed:X" - the fisheye image is over exposed with average pixel value X px.
	   * "FisheyeUnderExposed:X" - the fisheye image is under exposed with average pixel value X px.
	   * "ColorOverExposed:X" - the color image is over exposed with average pixel value X px.
	   * "ColorUnderExposed:X" - the color image is under exposed with average pixel value X px.
	   * "TooFewFeaturesTracked:X" - too few features were tracked in the fisheye image. The number of features tracked is X.
	   * "AreaDescriptionSaveProgress:X" - ADF saving is X * 100 percent complete.
	   * "Unknown"
	   */
	  if(key.equals("TangoServiceException"))
			  Toast.makeText(this, String.format("Tango service exception: %s", value), Toast.LENGTH_SHORT).show();
	  else if(key.equals("FisheyeOverExposed"))
			  ;//Toast.makeText(this, String.format("The fisheye image is over exposed with average pixel value %s px.", value), Toast.LENGTH_SHORT).show();
	  else if(key.equals("FisheyeUnderExposed"))
			  ;//Toast.makeText(this, String.format("The fisheye image is under exposed with average pixel value %s px.", value), Toast.LENGTH_SHORT).show();
	  else if(key.equals("ColorOverExposed")) 
			  ;//Toast.makeText(this, String.format("The color image is over exposed with average pixel value %s px.", value), Toast.LENGTH_SHORT).show();
	  else if(key.equals("ColorUnderExposed")) 
			  ;//Toast.makeText(this, String.format("The color image is under exposed with average pixel value %s px.", value), Toast.LENGTH_SHORT).show();
	  else if(key.equals("CameraTango")) 
		  	 Toast.makeText(this, value, Toast.LENGTH_SHORT).show();
	  else if(key.equals("TooFewFeaturesTracked")) 
	  {
		  if(!value.equals("0"))
			  Toast.makeText(this, String.format("Too few features (%s) were tracked in the fisheye image. This may result in poor odometry!", value), Toast.LENGTH_SHORT).show();
	  }
	  else
			  Toast.makeText(this, String.format("Unknown Tango event detected!? (type=%d)", type), Toast.LENGTH_SHORT).show();
  }
  
  //called from jni
  public void tangoEventCallback(
		  final int type, 
		  final String key,
		  final String value)
  {
	  Log.i(TAG, String.format("tangoEventCallback()"));

	  runOnUiThread(new Runnable() {
		    public void run() {
		    	tangoEventUI(type, key, value);
		    } 
		});
  }
  
  private boolean CheckTangoCoreVersion(int minVersion) {
    int versionNumber = 0;
    String packageName = TANGO_PACKAGE_NAME;
    try {
      PackageInfo pi = getApplicationContext().getPackageManager().getPackageInfo(packageName,
          PackageManager.GET_META_DATA);
      versionNumber = pi.versionCode;
    } catch (NameNotFoundException e) {
      e.printStackTrace();
    }
    return (minVersion <= versionNumber);
  }
  
  private RTABMapActivity getActivity() {return this;}
  
  private String[] loadFileList(String directory) {
	    File path = new File(directory); 
	    String fileList[];
	    try {
	    	path.mkdirs();
	    }
	    catch(SecurityException e) {
	        Log.e(TAG, "unable to write on the sd card " + e.toString());
	    }
	    if(path.exists()) {
	        FilenameFilter filter = new FilenameFilter() {

	            @Override
	            public boolean accept(File dir, String filename) {
	                File sel = new File(dir, filename);
	                return filename.endsWith(".db") || sel.isDirectory();
	            }

	        };
	        fileList = path.list(filter);
	    }
	    else {
	    	fileList = new String[0];
	    }
	    return fileList;
	}
  
  public boolean onOptionsItemSelected(MenuItem item) {
	  Log.i(TAG, "called onOptionsItemSelected; selected item: " + item);
	  int itemId = item.getItemId();
      if (itemId == R.id.pause)
      {
    	  item.setChecked(!item.isChecked());
    	  mItemSave.setEnabled(item.isChecked());
		  mItemExport.setEnabled(item.isChecked());
    	  mItemOpen.setEnabled(item.isChecked());
    	 // mItemSave.setEnabled(item.isChecked() && !mWorkingDirectory.isEmpty());
    	  if(item.isChecked())
    	  {
    		  RTABMapLib.setPausedMapping(true);
    		  ((TextView)findViewById(R.id.status)).setText("Paused");
    	  }
    	  else
    	  {
    		  RTABMapLib.setPausedMapping(false);
    		  ((TextView)findViewById(R.id.status)).setText(mItemLocalizationMode.isChecked()?"Localization":"Mapping");
    	  }
      } 
      else if(itemId == R.id.debug)
      {
    	  item.setChecked(!item.isChecked());
    	  if(!item.isChecked())
    	  {
    		  mLayoutDebug.setVisibility(LinearLayout.GONE);
    	  }
    	  else
    	  {
    		  mLayoutDebug.setVisibility(LinearLayout.VISIBLE);
    	  }
      }
      else if(itemId == R.id.mesh_rendering)
      {
    	  item.setChecked(!item.isChecked());
    	  RTABMapLib.setMeshRendering(item.isChecked());
      }
      else if(itemId == R.id.map_shown)
      {
    	  item.setChecked(!item.isChecked());
    	  RTABMapLib.setMapCloudShown(item.isChecked());
      }
      else if(itemId == R.id.odom_shown)
      {
    	  item.setChecked(!item.isChecked());
    	  RTABMapLib.setOdomCloudShown(item.isChecked());
      }
      else if(itemId == R.id.localization_mode)
      {
    	  item.setChecked(!item.isChecked());
    	  RTABMapLib.setLocalizationMode(item.isChecked());
      }
      else if(itemId == R.id.trajectory_mode)
      {
    	  item.setChecked(!item.isChecked());
    	  RTABMapLib.setTrajectoryMode(item.isChecked());
      }
      else if(itemId == R.id.graph_optimization)
      {
    	  item.setChecked(!item.isChecked());
    	  RTABMapLib.setGraphOptimization(item.isChecked());
      }
      else if(itemId == R.id.graph_visible)
      {
    	  item.setChecked(!item.isChecked());
    	  RTABMapLib.setGraphVisible(item.isChecked());
      }
      else if(itemId == R.id.auto_exposure)
      {
    	  item.setChecked(!item.isChecked());
    	  RTABMapLib.setAutoExposure(item.isChecked());
      }
      else if (itemId == R.id.save)
      {
    	  if(mOpenedDatabasePath.isEmpty())
    	  {
			  AlertDialog.Builder builder = new AlertDialog.Builder(this);
			  builder.setTitle("RTAB-Map Database Name (*.db):");
			  final EditText input = new EditText(this);
			  input.setInputType(InputType.TYPE_CLASS_TEXT);        
			  builder.setView(input);
			  builder.setPositiveButton("OK", new DialogInterface.OnClickListener() {
				  @Override
				  public void onClick(DialogInterface dialog, int which)
				  {
					  final String fileName = input.getText().toString();  
					  dialog.dismiss();
					  if(!fileName.isEmpty())
					  {
						  File newFile = new File(mWorkingDirectory + fileName + ".db");
						  if(newFile.exists())
						  {
							  new AlertDialog.Builder(getActivity())
				                .setTitle("File Already Exists")
				                .setMessage("Do you want to overwrite the existing file?")
				                .setPositiveButton("Yes", new DialogInterface.OnClickListener() {
				                    public void onClick(DialogInterface dialog, int which) {
				                    	mNewDatabasePath = mWorkingDirectory + fileName + ".db";
				                    	
				                    	mProgressDialog.setTitle("Saving");
				                  	    mProgressDialog.setMessage(String.format("Please wait while saving \"%s\"...", mNewDatabasePath));
				                  	    mProgressDialog.show();
				                    	
										RTABMapLib.save(); // send save event
										//disable gui actions
										mItemSave.setEnabled(false);
										mItemOpen.setEnabled(false);
										mItemExport.setEnabled(false);
				                    }
				                })
				                .setNegativeButton("No", new DialogInterface.OnClickListener() {
				                    public void onClick(DialogInterface dialog, int which) {
				                        dialog.dismiss();
				                    }
				                })
				                .show();
						  }
						  else
						  {
							  mNewDatabasePath = mWorkingDirectory + fileName + ".db";
							  
							  mProgressDialog.setTitle("Saving");
				        	  mProgressDialog.setMessage(String.format("Please wait while saving \"%s\"...", mNewDatabasePath));
				        	  mProgressDialog.show();
							  
							  RTABMapLib.save(); // send save event
							  //disable gui actions
							  mItemSave.setEnabled(false);
							  mItemOpen.setEnabled(false);
							  mItemExport.setEnabled(false);
						  }
					  }
				  }
			  });
			  builder.show();
    	  }
    	  else
    	  {
    		  mProgressDialog.setTitle("Saving");
        	  mProgressDialog.setMessage(String.format("Please wait while updating \"%s\"...", mOpenedDatabasePath));
        	  mProgressDialog.show();
        	  
    		  RTABMapLib.save(); // send save event
    		  //disable gui actions
			  mItemSave.setEnabled(false);
			  mItemOpen.setEnabled(false);
			  mItemExport.setEnabled(false);
    	  }
      }
      else if(itemId == R.id.reset)
      {
    	  ((TextView)findViewById(R.id.points)).setText(String.valueOf(0));
    	  ((TextView)findViewById(R.id.polygons)).setText(String.valueOf(0));
    	  ((TextView)findViewById(R.id.nodes)).setText(String.valueOf(0));
    	  ((TextView)findViewById(R.id.words)).setText(String.valueOf(0));
    	  ((TextView)findViewById(R.id.memory)).setText(String.valueOf(Debug.getNativeHeapAllocatedSize()/(1024*1024)));
    	  ((TextView)findViewById(R.id.db_size)).setText(String.valueOf(0));
    	  ((TextView)findViewById(R.id.inliers)).setText(String.valueOf(0));
    	  ((TextView)findViewById(R.id.features)).setText(String.valueOf(0));
    	  ((TextView)findViewById(R.id.update_time)).setText(String.valueOf(0));
    	  ((TextView)findViewById(R.id.hypothesis)).setText(String.valueOf(0));
		  mTotalLoopClosures = 0;
		  ((TextView)findViewById(R.id.total_loop)).setText(String.valueOf(mTotalLoopClosures));
    	  
		  if(mOpenedDatabasePath.isEmpty())
		  {
			  RTABMapLib.resetMapping();
		  }
		  else
		  {
			  mOpenedDatabasePath = "";
        	  RTABMapLib.openDatabase(mTempDatabasePath);
		  }
      }
      else if(itemId == R.id.export)
      {
    	  AlertDialog.Builder builder = new AlertDialog.Builder(this);
		  builder.setTitle("File Name (*.obj):");
		  final EditText input = new EditText(this);
		  input.setInputType(InputType.TYPE_CLASS_TEXT);        
		  builder.setView(input);
		  builder.setPositiveButton("OK", new DialogInterface.OnClickListener() {
			  @Override
			  public void onClick(DialogInterface dialog, int which)
			  {
				  final String fileName = input.getText().toString();    
				  dialog.dismiss();
				  if(!fileName.isEmpty())
				  {
					  File newFile = new File(mWorkingDirectory + fileName + ".obj");
					  if(newFile.exists())
					  {
						  new AlertDialog.Builder(getActivity())
			                .setTitle("File Already Exists")
			                .setMessage("Do you want to overwrite the existing file?")
			                .setPositiveButton("Yes", new DialogInterface.OnClickListener() {
			                    public void onClick(DialogInterface dialog, int which) {
			                    	final String path = mWorkingDirectory + fileName + ".obj";
			                    	
			                    	mItemExport.setEnabled(false);
			                    			                    	
			                    	mProgressDialog.setTitle("Exporting");
			                  	    mProgressDialog.setMessage(String.format("Please wait while exporting \"%s\"...", fileName+".obj"));
			                  	    mProgressDialog.show();
			                  	  
			                    	Thread exportThread = new Thread(new Runnable() {
			                		    public void run() {
			                		    	final boolean success = RTABMapLib.exportMesh(path);
			                		    	runOnUiThread(new Runnable() {
			                		    		public void run() {
					                		    	if(success)
							                    	{
							                    		Toast.makeText(getActivity(), String.format("Mesh \"%s\" successfully exported!", path), Toast.LENGTH_LONG).show();
							                    	}
							                    	else
							                    	{
							                    		Toast.makeText(getActivity(), String.format("Exporting mesh to \"%s\" failed!", path), Toast.LENGTH_LONG).show();
							                    	}
					                		    	mItemExport.setEnabled(true);
					                		    	mProgressDialog.dismiss();
			                		    		}
			                		    	});
			                		    } 
			                		});
			                		exportThread.start();
			                    }
			                })
			                .setNegativeButton("No", new DialogInterface.OnClickListener() {
			                    public void onClick(DialogInterface dialog, int which) {
			                        dialog.dismiss();
			                    }
			                })
			                .show();
					  }
					  else
					  {
						  final String path = mWorkingDirectory + fileName + ".obj";
						  mItemExport.setEnabled(false);
						  mProgressDialog.setTitle("Exporting");
	                  	  mProgressDialog.setMessage(String.format("Please wait while exporting \"%s\"...", fileName+".obj"));
	                  	  mProgressDialog.show();
						  Thread exportThread = new Thread(new Runnable() {
	                		    public void run() {
	                		    	final boolean success = RTABMapLib.exportMesh(path);
	                		    	runOnUiThread(new Runnable() {
	                		    		public void run() {
			                		    	if(success)
					                    	{
					                    		Toast.makeText(getActivity(), String.format("Mesh \"%s\" successfully exported!", path), Toast.LENGTH_LONG).show();
					                    	}
					                    	else
					                    	{
					                    		Toast.makeText(getActivity(), String.format("Exporting mesh to \"%s\" failed!", path), Toast.LENGTH_LONG).show();
					                    	}
			                		    	mItemExport.setEnabled(true);
			                		    	mProgressDialog.dismiss();
	                		    		}
	                		    	});
	                		    } 
	                		});
	                		exportThread.start();
					  }
				  }
			  }
		  });
		  builder.show();
      }
      else if(itemId == R.id.open)
      {
    	  final String[] files = loadFileList(mWorkingDirectory);
    	  if(files.length > 0)
    	  {
    		  AlertDialog.Builder builder = new AlertDialog.Builder(this);
    		  builder.setTitle("Choose your file");
              builder.setItems(files, new DialogInterface.OnClickListener() {
                  public void onClick(DialogInterface dialog, int which) {
                	  mOpenedDatabasePath = mWorkingDirectory + files[which];
                	  
                	  if(!mItemTrajectoryMode.isChecked())
                	  {
	                	  mProgressDialog.setTitle("Loading");
	                	  mProgressDialog.setMessage(String.format("Please wait while loading \"%s\"...", files[which]));
	                	  mProgressDialog.show();
                	  }

                	  RTABMapLib.openDatabase(mOpenedDatabasePath);
                	  RTABMapLib.setCamera(1);
                	  
                	  File extStore = new File(mTempDatabasePath);
                  	  if(extStore.exists())
                  	  {
                          extStore.delete();
                  	  }  
                  }
              });
              builder.show();
    	  }   	
      }
      else if(itemId == R.id.about)
      {
    	  AboutDialog about = new AboutDialog(this);
    	  about.setTitle("About RTAB-Map");
    	  about.show();
      }

      return true;
  }
}
