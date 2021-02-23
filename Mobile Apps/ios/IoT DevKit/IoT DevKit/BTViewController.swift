//
//  BTViewController.swift
//  IoT DevKit
//
//  Created by Robert Miller on 5/19/20.
//  Copyright © 2020 Robert Miller. All rights reserved.
//

import UIKit
import CoreBluetooth

class BTViewController: UIViewController {
    
    @IBOutlet weak var btCo2Label: UILabel!
    @IBOutlet weak var btTemperatureLabel: UILabel!
    @IBOutlet weak var btPressureLabel: UILabel!
    @IBOutlet weak var btHumidityLabel: UILabel!
    @IBOutlet weak var btBatteryLabel: UILabel!
    @IBOutlet weak var btTimestampLabel: UILabel!
    
    var centralManager: CBCentralManager!
    var co2Sensor: CBPeripheral!
    var readCharacteristic: CBCharacteristic!
    var writeCharacteristic: CBCharacteristic!
    var payload: Data!

    override func viewDidLoad() {
        super.viewDidLoad()

        // Do any additional setup after loading the view.
        payload = Data()
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }
    
    @IBAction func getBtCo2(_ sender: UIButton) {
        getValuesFromBtServer("co2")
    }
    
    @IBAction func getBtTemperature(_ sender: UIButton) {
        getValuesFromBtServer("temperature")
    }
    
    @IBAction func getBtPressure(_ sender: UIButton) {
        getValuesFromBtServer("pressure")
    }
    
    @IBAction func getBtHumidity(_ sender: UIButton) {
        getValuesFromBtServer("humidity")
    }
    
    @IBAction func getBtBattery(_ sender: UIButton) {
        getValuesFromBtServer("battery")
    }
    
    @IBAction func getBtAllValues(_ sender: UIButton) {
        getValuesFromBtServer("all")
    }
    
    func setBtLabelsWithDictionary(_ updatedBtValues: Dictionary<String, String>) {
        if let co2 = updatedBtValues["co2"] {
            btCo2Label.text = co2 + "ppm";
        }
        if let temperature = updatedBtValues["temperature"] {
            btTemperatureLabel.text = temperature + " °"
        }
        if let pressure = updatedBtValues["pressure"] {
            btPressureLabel.text = pressure + " hPa"
        }
        if let humidity = updatedBtValues["humidity"] {
            btHumidityLabel.text = humidity + " %"
        }
        if let battery = updatedBtValues["battery"] {
            btBatteryLabel.text = battery + "mV"
        }
        if let timestamp = updatedBtValues["timestamp"] {
            btTimestampLabel.text = timestamp
        }
    }
    
    func getValuesFromBtServer(_ path: String) {
        co2Sensor.writeValue(path.data(using: .utf8)!, for: writeCharacteristic, type: .withResponse)
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

extension BTViewController: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .unknown:
            print("state is unknown")
        case .resetting:
            print("state is resetting")
        case .unsupported:
            print("state is unsupported")
        case .unauthorized:
            print("state is unauthorized")
        case .poweredOff:
            print("state is powered off")
        case .poweredOn:
            print("state is powered on")
            centralManager.scanForPeripherals(withServices: nil, options: nil)
        default:
            print("default")
        }
    }
    
    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String : Any], rssi RSSI: NSNumber) {
        print("peripheral result")
        if peripheral.name == "co2sensor" {
            centralManager.stopScan()
            self.co2Sensor = peripheral
            centralManager.connect(peripheral, options: nil)
        }
    }
    
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.delegate = self
        peripheral.discoverServices(nil)
    }
}

extension BTViewController: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let services = peripheral.services else { return }
        
        for service in services {
            peripheral.discoverCharacteristics(nil, for: service)
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard let characteristics = service.characteristics else {return}
        
        for characteristic in characteristics {
            if characteristic.properties.contains(.write) {
                self.writeCharacteristic = characteristic
            } else {
                self.readCharacteristic = characteristic
                peripheral.setNotifyValue(true, for: characteristic)
                peripheral.readValue(for: characteristic)
            }
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if characteristic.value == nil {
            return
        }
        
        var payloadComplete = false
        
        for character in characteristic.value! {
            if character == 0x00 {
                payloadComplete = true
                break
            }
        }
        //payload += characteristic.value!
        payload.append(characteristic.value!)
        
        if payloadComplete {
            do {
                if let jsonReponse = try JSONSerialization.jsonObject(with: payload!, options: []) as? Dictionary<String, String> {
                    DispatchQueue.main.async {
                        self.setBtLabelsWithDictionary(jsonReponse)
                    }
                    payload = Data()
                }
            } catch let jserror as NSError {
                print(jserror.localizedDescription)
            }
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor descriptor: CBDescriptor, error: Error?) {
        peripheral.readValue(for: readCharacteristic)
    }
}
