package com.example.co2meterdemoapplication.ui.main;

import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.net.Uri;
import android.os.AsyncTask;
import android.os.Bundle;

import androidx.fragment.app.Fragment;

import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;

import com.example.co2meterdemoapplication.MainActivity;
import com.example.co2meterdemoapplication.R;

import org.apache.http.params.HttpParams;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.text.SimpleDateFormat;
import java.util.Date;

/**
 * A simple {@link Fragment} subclass.
 * Activities that contain this fragment must implement the
 * {@link WiFiDemoFragment.OnFragmentInteractionListener} interface
 * to handle interaction events.
 * Use the {@link WiFiDemoFragment#newInstance} factory method to
 * create an instance of this fragment.
 */
public class WiFiDemoFragment extends Fragment {
    // TODO: Rename parameter arguments, choose names that match
    // the fragment initialization parameters, e.g. ARG_ITEM_NUMBER
    private static final String ARG_PARAM1 = "param1";
    private static final String ARG_PARAM2 = "param2";
    private static Context mContext;

    // TODO: Rename and change types of parameters
    private String mParam1;
    private String mParam2;

    public WiFiDemoFragment() {
        // Required empty public constructor
    }

    /**
     * Use this factory method to create a new instance of
     * this fragment using the provided parameters.
     *
     * @param param1 Parameter 1.
     * @param param2 Parameter 2.
     * @return A new instance of fragment WiFiDemoFragment.
     */
    // TODO: Rename and change types and number of parameters
    public static WiFiDemoFragment newInstance(String param1, String param2) {
        WiFiDemoFragment fragment = new WiFiDemoFragment();
        Bundle args = new Bundle();
        args.putString(ARG_PARAM1, param1);
        args.putString(ARG_PARAM2, param2);
        fragment.setArguments(args);
        return fragment;
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (getArguments() != null) {
            mParam1 = getArguments().getString(ARG_PARAM1);
            mParam2 = getArguments().getString(ARG_PARAM2);
        }
    }

    public void setContext(Context context) {
        mContext = context;
    }

    private void getRequest(final String endPoint) {
        AsyncTask asyncTask = new AsyncTask() {
            @Override
            protected Object doInBackground(Object[] objects) {
                try {
                    MainActivity mainActivity = (MainActivity)mContext;
                    String urlString = "http://" + mainActivity.mEspAddress + "/values/" + endPoint;
                    //String urlString = "http://esp32.local/values/" + endPoint;
                    URL url = new URL(urlString);
                    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
                    //connection.setRequestMethod("GET");
                    //connection.setDoOutput(true);
                    //connection.setDoInput(true);
                    connection.setConnectTimeout(10000);
                    connection.setReadTimeout(10000);
                    connection.setRequestProperty("User-Agent","Mozilla/5.0 ( compatible ) ");
                    connection.setRequestProperty("Accept","*/*");
                    connection.connect();
                    BufferedReader rd = new BufferedReader(new InputStreamReader(connection.getInputStream()));
                    String content = "", line;
                    while ((line = rd.readLine()) != null) {
                        content += line + "\n";
                    }

                    final String finalContent = content;
                    new Handler(Looper.getMainLooper()).post(new Runnable() {
                        @Override
                        public void run() {
                            setTextViews(finalContent);
                        }
                    });
                } catch (Exception e) {
                    Log.d("WIFI", "doInBackground: " + e);
                }
                return null;
            }
        };
        asyncTask.execute();
    }

    private void setTextViews(String content) {
        try {
            JSONObject jsonObject = new JSONObject(content);
            final TextView co2TextView = getView().findViewById(R.id.wifi_fragment_co2_text_view);
            final TextView temperatureTextView = getView().findViewById(R.id.wifi_fragment_temperature_text_view);
            final TextView pressureTextView = getView().findViewById(R.id.wifi_fragment_pressure_text_view);
            final TextView humidityTextView = getView().findViewById(R.id.wifi_fragment_humidity_text_view);
            final TextView timestampTextView = getView().findViewById(R.id.wifi_fragment_timestamp_text_view);
            final TextView batteryTextView = getView().findViewById(R.id.wifi_fragment_battery_text_view);

            if (jsonObject.has("co2")) {
                co2TextView.setText(jsonObject.getString("co2"));
            }
            if (jsonObject.has("temperature")) {
                temperatureTextView.setText(jsonObject.getString("temperature"));
            }
            if (jsonObject.has("pressure")) {
                pressureTextView.setText(jsonObject.getString("pressure"));
            }
            if (jsonObject.has("humidity")) {
                humidityTextView.setText(jsonObject.getString("humidity"));
            }
            if (jsonObject.has("battery")) {
                batteryTextView.setText(jsonObject.getString("battery"));
            }
            if (jsonObject.has("timestamp")) {
                Long unixTimestamp = 1000*Long.valueOf(jsonObject.getString("timestamp").replace(" ", ""));
                Date date = new Date(unixTimestamp);
                //timestampTextView.setText(new SimpleDateFormat("EEEE MMMM d, yyyy hh:mm").format(date));
                timestampTextView.setText(date.toString());
            }
        } catch (Exception e) {
            Log.d("WiFiDemo", "setTextViews: " + e.getMessage());
        }
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {
        // Inflate the layout for this fragment
        final View view = inflater.inflate(R.layout.fragment_wi_fi_demo, container, false);

        Button getAllButton = view.findViewById(R.id.wifi_fragment_all_button);
        getAllButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                getRequest("all");
            }
        });

        Button getCo2Button = view.findViewById(R.id.wifi_fragment_co2_button);
        getCo2Button.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                getRequest("co2");
            }
        });

        Button getTemperatureButton = view.findViewById(R.id.wifi_fragment_temperature_button);
        getTemperatureButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                getRequest("temperature");
            }
        });

        Button getPressureButton = view.findViewById(R.id.wifi_fragment_pressure_button);
        getPressureButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                getRequest("pressure");
            }
        });

        Button getHumidityButton = view.findViewById(R.id.wifi_fragment_humidity_button);
        getHumidityButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                getRequest("humidity");
            }
        });

        Button getBatteryButton = view.findViewById(R.id.wifi_fragment_battery_button);
        getBatteryButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                getRequest("battery");
            }
        });

        Button nitrogenCalibrateButton = view.findViewById(R.id.zero_cal_button);
        nitrogenCalibrateButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                getRequest("zero");
            }
        });

        Button ambientCalibrateButton = view.findViewById(R.id.amb_cal_button);
        ambientCalibrateButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                getRequest("ambient");
            }
        });

        final Context thisContext = getContext();
        Button targetCalibrateButton = view.findViewById(R.id.target_cal_button);
        targetCalibrateButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                AlertDialog.Builder builder = new AlertDialog.Builder(thisContext)
                        .setTitle("Target Calibration")
                        .setMessage("Type the calibration gas concentration");

                final EditText input = new EditText(thisContext);
                builder.setView(input);
                builder.setPositiveButton("Calibrate", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialogInterface, int i) {
                        getRequest("calibrate?cal="+input.getText().toString());
                    }
                });
                builder.setNegativeButton("Cancel", null);
                builder.show();
            }
        });

        return view;
    }
}
