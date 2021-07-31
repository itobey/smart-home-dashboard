# read the battery percentage and send it to mqtt

import psutil
import paho.mqtt.client as paho

battery = psutil.sensors_battery()
plugged = battery.power_plugged
percent = str(battery.percent)
plugged = "Plugged In" if plugged else "Not Plugged In"
print(percent+'% | '+plugged)

broker="192.168.0.34"
port=1883
def on_publish(client,userdata,result):             #create function for callback
    print("data published \n")
    pass
client1= paho.Client("zenbook")                           #create client object
client1.on_publish = on_publish                          #assign function to callback
client1.connect(broker,port)                                 #establish connection
ret= client1.publish("home/zenbook", "zenbook battery=" + percent)                   #publish