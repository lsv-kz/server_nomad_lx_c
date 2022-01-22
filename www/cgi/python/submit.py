#!/usr/bin/env python3

import os, datetime 
import cgi
import codecs,sys

sys.stdout = codecs.getwriter("utf-8")(sys.stdout.detach(), errors='replace')
form = cgi.FieldStorage()
text1 = form.getfirst("TEXT_1", "?")
text2 = form.getfirst("TEXT_2", "?")
try:
	meth = os.environ['REQUEST_METHOD']
except:
	meth = 'get'

print("Content-Type: text/html\n")

print("""<!DOCTYPE HTML>
<html>
  <head>
   <meta charset="utf-8">
   <title>Test Form</title>
  </head>
  <body>
   <meta charset=\"utf-8\">""")
print("   <h3>","Hello ",text1,' ', text2, "!</h3>")
print("   <p>   len=%d</p>"%len(text1))
print("""   <form action=\"/cgi-bin/python/submit.py\" method=\"%s\" >
    <input type=\"text\" name=\"TEXT_1\" value=\"\">
    <input type=\"text\" name=\"TEXT_2\" value=\"\"><br>
    <input type=\"submit\">
   </form>""" %(meth))

now = datetime.datetime.utcnow()
print("""   <hr>
   %s
  </body>
</html>""" %now.strftime("%a, %d %b %Y %H:%M:%S GMT"))
