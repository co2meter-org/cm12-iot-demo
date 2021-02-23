//
//  WiFiViewController.swift
//  IoT DevKit
//
//  Created by Robert Miller on 5/19/20.
//  Copyright © 2020 Robert Miller. All rights reserved.
//

import UIKit

class WiFiViewController: UIViewController {
    
    @IBOutlet weak var wifiCo2Label : UILabel!
    @IBOutlet weak var wifiTemperatureLabel : UILabel!
    @IBOutlet weak var wifiPressureLabel : UILabel!
    @IBOutlet weak var wifiHumidityLabel : UILabel!
    @IBOutlet weak var wifiBatteryLabel : UILabel!
    @IBOutlet weak var wifiTimestampLabel : UILabel!

    override func viewDidLoad() {
        super.viewDidLoad()

        // Do any additional setup after loading the view.
    }
    
    @IBAction func getWiFiCO2(_ sender: UIButton) {
        getValuesFromWiFiServer("co2")
    }
    
    @IBAction func getWiFiTemperature(_ sender: UIButton) {
        getValuesFromWiFiServer("temperature")
    }
    
    @IBAction func getWiFiPressure(_ sender: UIButton) {
        getValuesFromWiFiServer("pressure")
    }
    
    @IBAction func getWiFiHumidity(_ sender: UIButton) {
        getValuesFromWiFiServer("humidity")
    }
    
    @IBAction func getWiFiBatteryValues(_ sender: UIButton) {
        getValuesFromWiFiServer("battery")
    }
    
    @IBAction func getWiFiAllValues(_ sender: UIButton) {
        getValuesFromWiFiServer("all")
    }
    
    @IBAction func zeroCalibrate(_ sender: UIButton) {
        getValuesFromWiFiServer("zero")
    }
    
    @IBAction func ambientCalibrate(_ sender: UIButton) {
        getValuesFromWiFiServer("ambient")
    }
    
    @IBAction func targetCalibrate(_ sender: UIButton) {
        //1. Create the alert controller.
        let alert = UIAlertController(title: "Target Calibration", message: "Enter the known calibration gas concentration in PPM", preferredStyle: .alert)

        //2. Add the text field. You can configure it however you need.
        alert.addTextField { (textField) in
            textField.text = ""
        }

        // 3. Grab the value from the text field, and print it when the user clicks OK.
        alert.addAction(UIAlertAction(title: "Calibrate", style: .default, handler: { [weak alert] (_) in
            let textField = alert?.textFields![0]
            let endpoint = "calibrate?cal=" + (textField?.text)!
            self.getValuesFromWiFiServer(endpoint)
        }))
        
        alert.addAction(UIAlertAction(title: "Cancel", style: .default, handler: nil))

        // 4. Present the alert.
        self.present(alert, animated: true, completion: nil)
    }
    
    func setWiFiLabelsWithDictionary(_ updatedWiFiValues: Dictionary<String, String>) {
        if let co2 = updatedWiFiValues["co2"] {
            wifiCo2Label.text = co2 + "ppm";
        }
        if let temperature = updatedWiFiValues["temperature"] {
            wifiTemperatureLabel.text = temperature + " °"
        }
        if let pressure = updatedWiFiValues["pressure"] {
            wifiPressureLabel.text = pressure + " hPa"
        }
        if let humidity = updatedWiFiValues["humidity"] {
            wifiHumidityLabel.text = humidity + " %"
        }
        if let battery = updatedWiFiValues["battery"] {
            wifiBatteryLabel.text = battery + " mV"
        }
        if let timestamp = updatedWiFiValues["timestamp"] {
            wifiTimestampLabel.text = timestamp
        }
    }
    
    func getValuesFromWiFiServer(_ path: String) {
        let url = URL(string: "http://esp32.local/values/" + path)
        guard let requestUrl = url else { fatalError() }
        
        var request = URLRequest(url: requestUrl)
        request.httpMethod = "GET"
        
        let task = URLSession.shared.dataTask(with: request) { (data, response, error) in
            if let error = error {
                print("Error took place \(error)")
                return
            }
            
            if let response = response as? HTTPURLResponse {
                print("Response HTTP Status code: \(response.statusCode)")
            }
            
            do {
                if let jsonReponse = try JSONSerialization.jsonObject(with: data!, options: []) as? Dictionary<String, String> {
                    DispatchQueue.main.async {
                        self.setWiFiLabelsWithDictionary(jsonReponse)
                    }
                }
            } catch let jserror as NSError {
                print(jserror.localizedDescription)
            }
        }
        task.resume()
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
