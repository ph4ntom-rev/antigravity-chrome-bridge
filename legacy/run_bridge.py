import sys
import os

# Import endpoints before starting to register them with the router
import api_chrome
import api_chrome_ext
from server import start_server

if __name__ == "__main__":
    start_server()
