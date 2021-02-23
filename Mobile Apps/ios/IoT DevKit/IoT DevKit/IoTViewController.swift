//
//  IoTViewController.swift
//  IoT DevKit
//
//  Created by Robert Miller on 5/19/20.
//  Copyright © 2020 Robert Miller. All rights reserved.
//

import UIKit
import AWSIoT

class IoTViewController: UIViewController {
    
    @IBOutlet weak var iotCo2Label : UILabel!
    @IBOutlet weak var iotTemperatureLabel : UILabel!
    @IBOutlet weak var iotPressureLabel : UILabel!
    @IBOutlet weak var iotHumidityLabel : UILabel!
    @IBOutlet weak var iotBatteryLabel : UILabel!
    @IBOutlet weak var iotTimestampLabel : UILabel!
    
    // Your Cognito Identity Pool
    let kIdentityPool = "your-identity-pool-id-here"
    // Constant Data Manager Key
    let kDataManagerKey = "kDataManager"
    // Your AWS IoT End Point
    let kIoTEndPointString = "wss://your-iot-endpoint-here.amazonaws.com/mqtt"
    
    override func viewDidLoad() {
        super.viewDidLoad()

        // Do any additional setup after loading the view.
        let credentials = AWSCognitoCredentialsProvider(regionType: .USEast2, identityPoolId: kIdentityPool)
        let configuration = AWSServiceConfiguration(region: .USEast2, credentialsProvider: credentials)
        
        AWSIoT.register(with: configuration!, forKey: "kAWSIoT")
        let iotEndPoint = AWSEndpoint(urlString: kIoTEndPointString)
        let iotDataConfiguration = AWSServiceConfiguration(region: .USEast2, endpoint: iotEndPoint, credentialsProvider: credentials)
        
        AWSIoTDataManager.register(with: iotDataConfiguration!, forKey: kDataManagerKey)
        
        getAWSClientID { (clientId, error) in
            if error == nil {
                self.connectToAWSIoT(clientId: clientId)
            }
        }
    }
    
    func getAWSClientID(completion: @escaping (_ clientId: String?,_ error: Error? ) -> Void) {
        let credentials = AWSCognitoCredentialsProvider(regionType: .USEast2, identityPoolId: kIdentityPool)
        
        credentials.getIdentityId().continueWith(block: { (task:AWSTask<NSString>) -> Any? in
            if let error = task.error as NSError? {
                print("Failed to get client ID => \(error)")
                completion(nil, error)
                return nil
            }
            
            let clientId = task.result! as String
            print("Got client ID => \(clientId)")
            completion(clientId, nil)
            return nil
        })
    }
    
    func connectToAWSIoT(clientId: String!) {
        
        func mqttEventCallback(_ status: AWSIoTMQTTStatus) {
            switch status {
            case .connecting: print("Connecting to AWS IoT")
            case .connected:
                print("Connected to AWS IoT")
                // Register subscriptions here
                // Publish a boot message if required
                self.registerSubscriptions()
            case .connectionError: print("AWS IoT connection error")
            case .connectionRefused: print("AWS IoT connection refused")
            case .protocolError: print("AWS IoT protocol error")
            case .disconnected: print("AWS IoT disconnected")
            case .unknown: print("AWS IoT unknown state")
            default:
                print("Error - unknown MQTT state")
            }
        }
        
        DispatchQueue.global(qos: .background).async {
            do {
                print("Attempting to connect to IoT device gateway with ID = \(clientId)")
                let dataManager = AWSIoTDataManager(forKey: "kDataManager")
                dataManager.connectUsingWebSocket(withClientId: clientId, cleanSession: true, statusCallback: mqttEventCallback)
            } catch {
                print("Error, failed to connect to device gateway => \(error)")
            }
        }
    }
    
    func registerSubscriptions() {
        func messageReceived(payload: Data) {
            let payloadDictionary = jsonDataToDict(jsonData: payload)
            print("Message received: \(payloadDictionary)")
            
            //Handle message event here...
            DispatchQueue.main.async {
                self.setIoTLabelsWithDictionary(payloadDictionary)
            }
        }
        
        let topicArray = ["$aws/things/co2-sensor/shadow/update"]
        let dataManager = AWSIoTDataManager(forKey: kDataManagerKey)
        
        for topic in topicArray {
            print("Registering subscription to => \(topic)")
            dataManager.subscribe(toTopic: topic, qoS: .messageDeliveryAttemptedAtLeastOnce, messageCallback: messageReceived)
        }
        
        dataManager.subscribe(toTopic: "$aws/things/co2-sensor/shadow/get/accepted", qoS: .messageDeliveryAttemptedAtLeastOnce, messageCallback: { (payload: Data) in
            let payloadDictionary = self.jsonDataToDict(jsonData: payload)
            print("Message received: \(payloadDictionary)")
            
            //Handle message event here...
            DispatchQueue.main.async {
                self.setIoTLabelsWithDictionary(payloadDictionary)
            }
        }, ackCallback: {
            dataManager.publishData(Data(), onTopic: "$aws/things/co2-sensor/shadow/get", qoS: .messageDeliveryAttemptedAtLeastOnce)
        })
    }
    
    func jsonDataToDict(jsonData: Data?) -> Dictionary <String, Any> {
        do {
            let jsonObject = try JSONSerialization.jsonObject(with: jsonData!, options: JSONSerialization.ReadingOptions.mutableContainers) as! Dictionary<String,Any>
            let stateObject = jsonObject["state"] as? Dictionary<String,Any>
            let reportedObject = stateObject!["reported"] as? Dictionary<String,Any>
            
            //let jsonDict = try JSONSerialization.jsonObject(with: jsonData!, options: [])
            //let convertedDict = jsonDict as! [String: String]
            return reportedObject!
        } catch {
            print(error.localizedDescription)
            return [:]
        }
    }
    
    func publishMessage(message: String!, topic: String!) {
        let dataManager = AWSIoTDataManager(forKey: "kDataManager")
        dataManager.publishString(message, onTopic: topic, qoS: .messageDeliveryAttemptedAtLeastOnce)
    }
    
    func setIoTLabelsWithDictionary(_ updatedIoTValues: Dictionary<String, Any>) {
        if let co2 = updatedIoTValues["co2"] as! String? {
            iotCo2Label.text = co2 + "ppm";
        }
        if let temperature = updatedIoTValues["temperature"] as! String? {
            iotTemperatureLabel.text = temperature + " °"
        }
        if let pressure = updatedIoTValues["pressure"] as! String? {
            iotPressureLabel.text = pressure + " hPa"
        }
        if let humidity = updatedIoTValues["humidity"] as! String? {
            iotHumidityLabel.text = humidity + " %"
        }
        if let battery = updatedIoTValues["battery"] as! String? {
            iotBatteryLabel.text = battery + " mV"
        }
        if let timestamp = updatedIoTValues["timestamp"] as! String? {
            let timestampNumber = Double(timestamp.trimmingCharacters(in: .whitespacesAndNewlines))
            let date = Date(timeIntervalSince1970: timestampNumber!)
            let dateFormatter = DateFormatter()
            dateFormatter.timeStyle = DateFormatter.Style.medium
            dateFormatter.dateStyle = DateFormatter.Style.medium
            dateFormatter.timeZone = .current
            iotTimestampLabel.text = dateFormatter.string(from: date)
        }
    }
    

    /*
    // MARK: - Navigation

    // In a storyboard-based application, you will often want to do a little preparation before navigation
    override func prepare(for segue: UIStoryboardSegue, sender: Any?) {
        // Get the new view controller using segue.destination.
        // Pass the selected object to the new view controller.
    }
    */

}
