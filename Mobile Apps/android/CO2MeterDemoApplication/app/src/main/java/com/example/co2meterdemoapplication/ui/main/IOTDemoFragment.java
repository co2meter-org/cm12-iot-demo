package com.example.co2meterdemoapplication.ui.main;

import android.content.Context;
import android.net.Uri;
import android.os.Bundle;

import androidx.fragment.app.Fragment;

import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import com.amazonaws.auth.CognitoCachingCredentialsProvider;
import com.amazonaws.mobile.client.AWSMobileClient;
import com.amazonaws.mobile.client.Callback;
import com.amazonaws.mobile.client.UserStateDetails;
import com.amazonaws.mobileconnectors.iot.AWSIotKeystoreHelper;
import com.amazonaws.mobileconnectors.iot.AWSIotMqttClientStatusCallback;
import com.amazonaws.mobileconnectors.iot.AWSIotMqttLastWillAndTestament;
import com.amazonaws.mobileconnectors.iot.AWSIotMqttManager;
import com.amazonaws.mobileconnectors.iot.AWSIotMqttMessageDeliveryCallback;
import com.amazonaws.mobileconnectors.iot.AWSIotMqttNewMessageCallback;
import com.amazonaws.mobileconnectors.iot.AWSIotMqttQos;
import com.amazonaws.mobileconnectors.iot.AWSIotMqttSubscriptionStatusCallback;
import com.amazonaws.regions.Region;
import com.amazonaws.regions.Regions;
import com.amazonaws.services.iot.AWSIotClient;
import com.amazonaws.services.iot.model.AttachPrincipalPolicyRequest;
import com.amazonaws.services.iot.model.CreateKeysAndCertificateRequest;
import com.amazonaws.services.iot.model.CreateKeysAndCertificateResult;
import com.example.co2meterdemoapplication.R;

import org.json.JSONObject;

import java.io.UnsupportedEncodingException;
import java.security.KeyStore;
import java.util.Date;
import java.util.UUID;

/**
 * A simple {@link Fragment} subclass.
 * Activities that contain this fragment must implement the
 * {@link IOTDemoFragment.OnFragmentInteractionListener} interface
 * to handle interaction events.
 * Use the {@link IOTDemoFragment#newInstance} factory method to
 * create an instance of this fragment.
 */
public class IOTDemoFragment extends Fragment {
    // TODO: Rename parameter arguments, choose names that match
    // the fragment initialization parameters, e.g. ARG_ITEM_NUMBER
    private static final String ARG_PARAM1 = "param1";
    private static final String ARG_PARAM2 = "param2";

    static final String LOG_TAG = "IOTDemoFragment";

    // --- Constants to modify per your configuration ---

    // IoT endpoint
    // AWS Iot CLI describe-endpoint call returns: XXXXXXXXXX.iot.<region>.amazonaws.com
    private static final String CUSTOMER_SPECIFIC_ENDPOINT = "you-aws-endpoint-here.amazonaws.com";
    // Name of the AWS IoT policy to attach to a newly created certificate
    private static final String AWS_IOT_POLICY_NAME = "co2-sensor-policy";

    // Region of AWS IoT
    private static final Regions MY_REGION = Regions.US_EAST_2;
    // Filename of KeyStore file on the filesystem
    private static final String KEYSTORE_NAME = "iot_keystore";
    // Password for the private key in the KeyStore
    private static final String KEYSTORE_PASSWORD = "password";
    // Certificate and key aliases in the KeyStore
    private static final String CERTIFICATE_ID = "default";

    AWSIotClient mIotAndroidClient;
    AWSIotMqttManager mqttManager;
    String clientId;
    String keystorePath;
    String keystoreName;
    String keystorePassword;

    KeyStore clientKeyStore = null;
    String certificateId;

    // TODO: Rename and change types of parameters
    private String mParam1;
    private String mParam2;
    private Context mContext;

    public IOTDemoFragment() {
        // Required empty public constructor
    }

    /**
     * Use this factory method to create a new instance of
     * this fragment using the provided parameters.
     *
     * @param param1 Parameter 1.
     * @param param2 Parameter 2.
     * @return A new instance of fragment IOTDemoFragment.
     */
    // TODO: Rename and change types and number of parameters
    public static IOTDemoFragment newInstance(String param1, String param2) {
        IOTDemoFragment fragment = new IOTDemoFragment();
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

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {
        // Inflate the layout for this fragment
        return inflater.inflate(R.layout.fragment_iotdemo, container, false);
    }

    public void setContext(Context context) {
        mContext = context;
        clientId = UUID.randomUUID().toString();

        // Initialize the AWS Cognito credentials provider
        AWSMobileClient.getInstance().initialize(context, new Callback<UserStateDetails>() {
            @Override
            public void onResult(UserStateDetails result) {
                initIoTClient();
            }

            @Override
            public void onError(Exception e) {
                Log.e(LOG_TAG, "onError: ", e);
            }
        });
    }


    void subscribeToTopic() {
        try {
            String topic = "$aws/things/co2-sensor/shadow/update";
            mqttManager.subscribeToTopic(topic, AWSIotMqttQos.QOS0,
                    new AWSIotMqttNewMessageCallback() {
                        @Override
                        public void onMessageArrived(final String topic, final byte[] data) {
                            new Handler(Looper.getMainLooper()).post(new Runnable() {
                                @Override
                                public void run() {
                                    new Handler(Looper.getMainLooper()).post(new Runnable() {
                                        @Override
                                        public void run() {
                                            try {
                                                String message = new String(data, "UTF-8");
                                                Log.d(LOG_TAG, "Message arrived:");
                                                Log.d(LOG_TAG, "   Topic: " + topic);
                                                Log.d(LOG_TAG, " Message: " + message);

                                                JSONObject jsonObject = new JSONObject(message);
                                                JSONObject state = jsonObject.getJSONObject("state");
                                                JSONObject reported = state.getJSONObject("reported");

                                                TextView co2TextView = (TextView)getView().findViewById(R.id.co2);
                                                co2TextView.setText("CO2: " + reported.getString("co2"));

                                                TextView temperatureTextView = (TextView)getView().findViewById(R.id.temperature);
                                                temperatureTextView.setText("Temperature: " + reported.getString("temperature"));

                                                TextView humidityTextView = (TextView)getView().findViewById(R.id.humidity);
                                                humidityTextView.setText("Humidity: " + reported.getString("humidity"));

                                                TextView pressureTextView = (TextView)getView().findViewById(R.id.pressure);
                                                pressureTextView.setText("Pressure: " + reported.getString("pressure"));

                                                TextView batteryTextView = (TextView)getView().findViewById(R.id.battery);
                                                batteryTextView.setText("Battery: " + reported.getString("battery"));
                                            } catch (Exception e) {
                                                Log.d("IOTDemoFragment", "run: " + e);
                                            }
                                        }
                                    });
                                }
                            });
                        }
                    });
            mqttManager.subscribeToTopic("$aws/things/co2-sensor/shadow/get/accepted", AWSIotMqttQos.QOS0, new AWSIotMqttSubscriptionStatusCallback() {
                @Override
                public void onSuccess() {
                    mqttManager.publishData(new byte[0], "$aws/things/co2-sensor/shadow/get", AWSIotMqttQos.QOS0, new AWSIotMqttMessageDeliveryCallback() {
                        @Override
                        public void statusChanged(MessageDeliveryStatus status, Object userData) {
                            Log.d("PUBLISH_DATA", "statusChanged: " + status);
                        }
                    }, null);
                }

                @Override
                public void onFailure(Throwable exception) {

                }
            }, new AWSIotMqttNewMessageCallback() {
                @Override
                public void onMessageArrived(final String topic, final byte[] data) {
                    new Handler(Looper.getMainLooper()).post(new Runnable() {
                        @Override
                        public void run() {
                            try {
                                String message = new String(data, "UTF-8");
                                Log.d(LOG_TAG, "Message arrived:");
                                Log.d(LOG_TAG, "   Topic: " + topic);
                                Log.d(LOG_TAG, " Message: " + message);

                                JSONObject jsonObject = new JSONObject(message);
                                JSONObject state = jsonObject.getJSONObject("state");
                                JSONObject reported = state.getJSONObject("reported");

                                TextView co2TextView = (TextView)getView().findViewById(R.id.co2);
                                co2TextView.setText("CO2: " + reported.getString("co2"));

                                TextView temperatureTextView = (TextView)getView().findViewById(R.id.temperature);
                                temperatureTextView.setText("Temperature: " + reported.getString("temperature"));

                                TextView humidityTextView = (TextView)getView().findViewById(R.id.humidity);
                                humidityTextView.setText("Humidity: " + reported.getString("humidity"));

                                TextView pressureTextView = (TextView)getView().findViewById(R.id.pressure);
                                pressureTextView.setText("Pressure: " + reported.getString("pressure"));

                                TextView batteryTextView = (TextView)getView().findViewById(R.id.battery);
                                batteryTextView.setText("Battery: " + reported.getString("battery"));

                                TextView timestampTextView = (TextView)getView().findViewById(R.id.timestamp);
                                Long unixTimestamp = 1000*Long.valueOf(reported.getString("timestamp").replace(" ", ""));
                                Date date = new Date(unixTimestamp);
                                //timestampTextView.setText(new SimpleDateFormat("EEEE MMMM d, yyyy hh:mm").format(date));
                                timestampTextView.setText(date.toString());
                            } catch (Exception e) {
                                Log.d("IOTDemoFragment", "onMessageArrived: " + e);
                            }
                        }
                    });
                }
            });
        } catch (Exception e) {
            Log.e(LOG_TAG, "Subscription error.", e);
        }
    }

    void initIoTClient() {
        Region region = Region.getRegion(MY_REGION);

        // MQTT Client
        mqttManager = new AWSIotMqttManager(clientId, CUSTOMER_SPECIFIC_ENDPOINT);

        // Set keepalive to 10 seconds.  Will recognize disconnects more quickly but will also send
        // MQTT pings every 10 seconds.
        mqttManager.setKeepAlive(10);

        // Set Last Will and Testament for MQTT.  On an unclean disconnect (loss of connection)
        // AWS IoT will publish this message to alert other clients.
        AWSIotMqttLastWillAndTestament lwt = new AWSIotMqttLastWillAndTestament("my/lwt/topic",
                "Android client lost connection", AWSIotMqttQos.QOS0);
        mqttManager.setMqttLastWillAndTestament(lwt);

        // IoT Client (for creation of certificate if needed)
        mIotAndroidClient = new AWSIotClient(AWSMobileClient.getInstance());
        mIotAndroidClient.setRegion(region);

        try {
            // Initialize the Amazon Cognito credentials provider
            CognitoCachingCredentialsProvider credentialsProvider = new CognitoCachingCredentialsProvider(
                    getContext(),
                    "us-east-2:9554db2b-cb45-4868-8672-5ebdb59426d7", // Identity pool ID
                    Regions.US_EAST_2 // Region
            );
            mqttManager.connect(credentialsProvider, new AWSIotMqttClientStatusCallback() {
                @Override
                public void onStatusChanged(AWSIotMqttClientStatus status, Throwable throwable) {
                    Log.d(LOG_TAG, "Status = " + String.valueOf(status));
                    if (status.equals(AWSIotMqttClientStatus.Connected)) {
                        subscribeToTopic();
                    }
                }
            });
        } catch (final Exception e) {
            Log.e(LOG_TAG, "Connection error.", e);
        }
    }
}
