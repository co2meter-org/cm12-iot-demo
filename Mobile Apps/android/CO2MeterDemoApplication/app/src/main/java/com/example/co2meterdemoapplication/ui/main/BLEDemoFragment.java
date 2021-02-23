package com.example.co2meterdemoapplication.ui.main;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
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
import android.widget.Button;
import android.widget.TextView;

import com.example.co2meterdemoapplication.R;

import org.json.JSONObject;

import java.util.List;
import java.util.UUID;

/**
 * A simple {@link Fragment} subclass.
 * Activities that contain this fragment must implement the
 * {@link BLEDemoFragment.OnFragmentInteractionListener} interface
 * to handle interaction events.
 * Use the {@link BLEDemoFragment#newInstance} factory method to
 * create an instance of this fragment.
 */
public class BLEDemoFragment extends Fragment {
    // TODO: Rename parameter arguments, choose names that match
    // the fragment initialization parameters, e.g. ARG_ITEM_NUMBER
    private static final String ARG_PARAM1 = "param1";
    private static final String ARG_PARAM2 = "param2";

    // Bluetooth's variables
    BluetoothAdapter bluetoothAdapter;
    BluetoothLeScanner bluetoothLeScanner;
    BluetoothManager bluetoothManager;
    BluetoothScanCallback bluetoothScanCallback;
    BluetoothGatt gattClient;

    BluetoothGattCharacteristic characteristicID; // To get Value
    BluetoothGattCharacteristic characteristicIDTX;

    // UUID's (set yours)
    final UUID SERVICE_UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
    final UUID CHARACTERISTIC_UUID_ID = UUID.fromString("1a220d0a-6b06-4767-8692-243153d94d85");
    final UUID CHARACTERISTIC_UUID_RX = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
    final UUID CHARACTERISTIC_UUID_TX = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");
    //final UUID DESCRIPTOR_UUID_ID = UUID.fromString("ec6e1003-884b-4a1c-850f-1cfce9cf6567");
    final UUID DESCRIPTOR_UUID_ID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");

    public Context mContext;
    private final static String TAG = "BLEDemoFragment";

    private String payload = "";

    // TODO: Rename and change types of parameters
    private String mParam1;
    private String mParam2;

    public BLEDemoFragment() {
        // Required empty public constructor
    }

    /**
     * Use this factory method to create a new instance of
     * this fragment using the provided parameters.
     *
     * @param param1 Parameter 1.
     * @param param2 Parameter 2.
     * @return A new instance of fragment BLEDemoFragment.
     */
    // TODO: Rename and change types and number of parameters
    public static BLEDemoFragment newInstance(String param1, String param2) {
        BLEDemoFragment fragment = new BLEDemoFragment();
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

    private void setTextViews(String content) {
        try {
            JSONObject jsonObject = new JSONObject(content);
            final TextView co2TextView = getView().findViewById(R.id.ble_fragment_co2_text_view);
            final TextView temperatureTextView = getView().findViewById(R.id.ble_fragment_temperature_text_view);
            final TextView pressureTextView = getView().findViewById(R.id.ble_fragment_pressure_text_view);
            final TextView humidityTextView = getView().findViewById(R.id.ble_fragment_humidity_text_view);
            final TextView batteryTextView = getView().findViewById(R.id.ble_fragment_battery_text_view);
            final TextView timestampTextView = getView().findViewById(R.id.ble_fragment_timestamp_text_view);

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
                timestampTextView.setText(jsonObject.getString("timestamp"));
            }
        } catch (Exception e) {
            Log.d(TAG, "setTextViews: " + e);
        }
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {
        // Inflate the layout for this fragment
        // Bluetooth
        bluetoothManager = (BluetoothManager) mContext.getSystemService(Context.BLUETOOTH_SERVICE);
        bluetoothAdapter = bluetoothManager.getAdapter();
        startScan();

        final View view = inflater.inflate(R.layout.fragment_bledemo, container, false);

        Button getAllButton = view.findViewById(R.id.ble_fragment_all_button);
        getAllButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                characteristicIDTX.setValue("all");
                gattClient.writeCharacteristic(characteristicIDTX);
            }
        });

        Button getCo2Button = view.findViewById(R.id.ble_fragment_co2_button);
        getCo2Button.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                characteristicIDTX.setValue("co2");
                gattClient.writeCharacteristic(characteristicIDTX);
            }
        });

        Button getTemperatureButton = view.findViewById(R.id.ble_fragment_temperature_button);
        getTemperatureButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                characteristicIDTX.setValue("temperature");
                gattClient.writeCharacteristic(characteristicIDTX);
            }
        });

        Button getPressureButton = view.findViewById(R.id.ble_fragment_pressure_button);
        getPressureButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                characteristicIDTX.setValue("pressure");
                gattClient.writeCharacteristic(characteristicIDTX);
            }
        });

        Button getHumidityButton = view.findViewById(R.id.ble_fragment_humidity_button);
        getHumidityButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                characteristicIDTX.setValue("humidity");
                gattClient.writeCharacteristic(characteristicIDTX);
            }
        });

        Button getBatteryButton = view.findViewById(R.id.ble_fragment_battery_button);
        getBatteryButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                characteristicIDTX.setValue("battery");
                gattClient.writeCharacteristic(characteristicIDTX);
            }
        });

        return view;
    }


    // BLUETOOTH SCAN

    private void startScan(){
        Log.i(TAG,"startScan()");
        bluetoothScanCallback = new BluetoothScanCallback();
        bluetoothLeScanner = bluetoothAdapter.getBluetoothLeScanner();
        bluetoothLeScanner.startScan(bluetoothScanCallback);
    }

    // BLUETOOTH CONNECTION
    private void connectDevice(BluetoothDevice device) {
        if (device == null) Log.i(TAG,"Device is null");
        GattClientCallback gattClientCallback = new GattClientCallback();
        gattClient = device.connectGatt(mContext,false,gattClientCallback);
    }

    // BLE Scan Callbacks
    private class BluetoothScanCallback extends ScanCallback {


        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            Log.i(TAG, "onScanResult");
            if (result.getDevice().getName() != null){
                if (result.getDevice().getName().equals("co2sensor")) {
                    // When find your device, connect.
                    connectDevice((BluetoothDevice)result.getDevice());
                    bluetoothLeScanner.stopScan(bluetoothScanCallback); // stop scan
                }
            }
        }

        @Override
        public void onBatchScanResults(List<ScanResult> results) {
            Log.i(TAG, "onBathScanResults");
        }

        @Override
        public void onScanFailed(int errorCode) {
            Log.i(TAG, "ErrorCode: " + errorCode);
        }
    }

    // Bluetooth GATT Client Callback
    private class GattClientCallback extends BluetoothGattCallback {

        final static String TAG = "GattClientCallback";
        @Override
        public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
            super.onConnectionStateChange(gatt, status, newState);
            Log.i(TAG,"onConnectionStateChange");

            if (status == BluetoothGatt.GATT_FAILURE) {
                Log.i(TAG, "onConnectionStateChange GATT FAILURE");
                return;
            } else if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.i(TAG, "onConnectionStateChange != GATT_SUCCESS");
                return;
            }

            if (newState == BluetoothProfile.STATE_CONNECTED) {
                Log.i(TAG, "onConnectionStateChange CONNECTED");
                gatt.discoverServices();
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.i(TAG, "onConnectionStateChange DISCONNECTED");
            }
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt gatt, int status) {
            super.onServicesDiscovered(gatt, status);
            Log.i(TAG,"onServicesDiscovered");
            if (status != BluetoothGatt.GATT_SUCCESS) return;

            // Reference your UUIDs
            characteristicID = gatt.getService(SERVICE_UUID).getCharacteristic(CHARACTERISTIC_UUID_TX);
            gatt.setCharacteristicNotification(characteristicID,true);

            BluetoothGattDescriptor descriptor = characteristicID.getDescriptor(DESCRIPTOR_UUID_ID);
            descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
            gatt.writeDescriptor(descriptor);

            characteristicIDTX = gatt.getService(SERVICE_UUID).getCharacteristic(CHARACTERISTIC_UUID_RX);
        }

        @Override
        public void onCharacteristicRead(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic, int status) {
            super.onCharacteristicRead(gatt, characteristic, status);
            Log.i(TAG,"onCharacteristicRead");
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic, int status) {
            super.onCharacteristicWrite(gatt, characteristic, status);
            Log.i(TAG,"onCharacteristicWrite");
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic) {
            super.onCharacteristicChanged(gatt, characteristic);
            Log.i(TAG,"onCharacteristicChanged " + new String(characteristic.getValue()));
            // Here you can read the characteristc's value
            //setTextViews(new String(characteristic.getValue()));
            boolean payload_complete = false;
            String value = "";
            for (int i = 0; i < characteristic.getValue().length; i++) {
                Log.d(TAG, "onCharacteristicChanged: " + characteristic.getValue()[i]);
                if (characteristic.getValue()[i] == '\0') {
                    payload_complete = true;
                    break;
                }
                value = value + (char)characteristic.getValue()[i];
            }
            payload = payload + value;

            if (payload_complete) {
                final String finalValue = payload;
                new Handler(Looper.getMainLooper()).post(new Runnable() {
                    @Override
                    public void run() {
                        setTextViews(finalValue);
                        payload = "";
                    }
                });
            }
        }

        @Override
        public void onDescriptorRead(BluetoothGatt gatt, BluetoothGattDescriptor descriptor, int status) {
            super.onDescriptorRead(gatt, descriptor, status);
            Log.i(TAG,"onDescriptorRead");
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt gatt, BluetoothGattDescriptor descriptor, int status) {
            super.onDescriptorWrite(gatt, descriptor, status);
            Log.i(TAG,"onDescriptorWrite");
        }
    }
}
