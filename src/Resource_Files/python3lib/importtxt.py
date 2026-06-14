
def read_unicode(filepath):

    fp = open(filepath,"rb")
    data = fp.read()
    fp.close()

    text = ""
    for codec in ("UTF-8","GB18030","UTF-16"):
        try:
            text = data.decode(codec)
            break
        except:
            continue
    return text
