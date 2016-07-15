#!/usr/bin/env python

from pyvirtualdisplay import Display
from selenium import webdriver

display = Display(visible=0, size=(800, 600))
display.start()

browser = webdriver.Firefox()
browser.get('http://172.17.0.2/')
# browser.get('http://172.17.0.2/simplepage')
print "browser title:",
print browser.title
browser.quit()

display.stop()