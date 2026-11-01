import configparser
c = configparser.ConfigParser()
c.read('platformio.ini')
print('DEFAULT', dict(c.defaults()))
