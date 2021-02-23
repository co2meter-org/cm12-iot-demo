#!/usr/bin/python

import matplotlib.pyplot as plt
import sys, getopt
import csv
import requests
import json
import time

runtype = "datalogger"

def printHelpMenu():
  print("wifiplot Help Menu")
  print("\r\n*** General Usage ***")
  print("python wifiplot.py -args ...")
  print("\r\n*** Options ***")
  print("-h -> help: print this menu")
  print("")
  print("-p -> plot: requires input file")
  print("  usage: python wifiplot.py -p inputfile.csv")
  print("")
  print("-d -> datalogger: requires output file")
  print("  usage: python wifiplot.py -d outputfile.csv")
  print("  will continuously run and request data from device, save to log, and output to plot")
  print("")
  print("-g -> get: requires output file")
  print("  usage: pythong wifiploy.py -g outputfile.csv")
  print("  will get the data log file from the device, save it to a local file, and output to plot")
  print("-t -> time: sets the data log interval.  MUST be 2 seconds or greater..")


def plotvalues(title,axisnames,axes):
  plt.title(title)
  plt.xlabel(axisnames[0])
  legendnames = []
  for i in range(1,len(axisnames)):
    plt.plot(axes[0],axes[i])
    legendnames.append(axisnames[i])
  plt.legend(legendnames,loc='upper left')
  plt.pause(0.1)


def plotlog(inputfilename):
  print("Plot Log File: " + inputfilename)
  if inputfilename == '':
    printHelpMenu()

  with open(inputfilename) as csvfile:
    reader = csv.DictReader(csvfile)
    axesnames = reader.fieldnames
    axes = []
    print(axesnames)
    for i in range(len(axesnames)):
      axes.append(list())
    for row in reader:
      print(row)
      for i in range(len(axesnames)):
        print(i)
        axes[i].append(float(row[axesnames[i]]))
    plotvalues(inputfilename,axesnames,axes)


def datalog(outputfilename,period):
  if outputfilename == '':
    printHelpMenu()
    sys.exit()
  if period < 2:
    printHelpMenu()
    sys.exit()
  values = []
  with open(outputfilename, 'w') as csvfile:
    fieldnames = ['time','co2','temperature','humidity','pressure']
    writer = csv.DictWriter(csvfile,fieldnames=fieldnames)
    writer.writeheader()
    timelist = []
    co2 = []
    temperature = []
    humidity = []
    pressure = []
    while True:
      resp = requests.get('http://esp32.local/values/all')
      print(resp)
      values = json.loads(resp.text)
      writer.writerow({'time':values['timestamp'],'co2':values['co2'],'temperature':values['temperature'],'humidity':values['humidity'],'pressure':values['pressure']})

      timelist.append(float(values['timestamp']))
      co2.append(float(values['co2']))
      temperature.append(float(values['temperature']))
      humidity.append(float(values['humidity']))
      pressure.append(float(values['pressure'])/100)

      axes = [timelist, co2, temperature, humidity, pressure]
      plotvalues(outputfilename,fieldnames,axes)
      time.sleep(period)


def getdatalog(outputfilename):
  if outputfilename == '':
    printHelpMenu()
  resp = requests.get('http://esp32.local/values/log')
  with open(outputfilename,'wb') as f:
    f.write(resp.content)

def main(argv):
  try:
    opts, args = getopt.getopt(argv, "hp:d:g:",["ifile=","ofile=","per="])
  except getopt.GetoptError:
    print("exception while getting options and arguments")
    printHelpMenu()
  if len(opts) == 0:
    print("no options listed...")
    printHelpMenu()
    sys.exit(2)
  prog = sys.exit
  file = "log.csv"
  interval = 2
  for opt, arg in opts:
    if opt == '-h':
      printHelpMenu()
      sys.exit()
    elif opt in ("-p", "--ifile"):
      plotlog(arg)
      return
    elif opt in ("-d", "--ofile"):
      prog = datalog
      file = arg
    elif opt in ("-g", "--ofile"):
      getdatalog(arg)
      return
    elif opt in ("-t", "--per"):
      interval = arg
    else:
      printHelpMenu()
  prog(file,interval)

if __name__ == "__main__":
  main(sys.argv[1:])
