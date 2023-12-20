import unittest
import requests
import subprocess
import os
import time
import random
import re

class TestEndpoint(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Start the process with LD_PRELOAD
        cls.test_port = random.randint(8000, 9000)
        cls.process = subprocess.Popen(
            ['sh'],
            env={ **os.environ, "LD_PRELOAD": "./memodoor_inject.so", "MEMODOOR_PORT": str(cls.test_port) },
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            shell=False
        )

    @classmethod
    def tearDownClass(cls):
        # Terminate the process
        cls.process.terminate()
        cls.process.wait()

    def test_maps_endpoint(self):
        # Give the server some time to start
        time.sleep(1)

        # Make a request to the /maps endpoint
        response = requests.get(f'http://127.0.0.1:{TestEndpoint.test_port}/maps')

        # Verify that the response status code is 200
        self.assertEqual(response.status_code, 200)

        # print("got response:", response.text)

        # Check if the response looks like a maps file
        # Maps file lines typically look like: address perms offset dev inode pathname
        self.assertIn('/memodoor_inject.so', response.text)
        self.assertIn('rw-p', response.text)
        self.assertIn('r-xp', response.text)
        self.assertIn('r--p', response.text)
        lines = response.text.split('\n')
        self.assertTrue(len(lines) > 0)  # Ensure there's at least one line
        for line in lines:
            if line:  # Skip empty lines
                parts = line.split()
                self.assertTrue(len(parts) >= 5)  # Check for minimum number of components in a line
                self.assertIn('-', parts[0])  # Check for address range format

    def test_root_directory(self):
        response = requests.get(f'http://127.0.0.1:{TestEndpoint.test_port}/')
        self.assertEqual(response.status_code, 200)

    def test_env_endpoint(self):
        response = requests.get(f'http://127.0.0.1:{TestEndpoint.test_port}/env')
        self.assertEqual(response.status_code, 200)

    def test_not_found_endpoint(self):
        response = requests.get(f'http://127.0.0.1:{TestEndpoint.test_port}/not_found')
        self.assertEqual(response.status_code, 404)

    def test_threads_endpoint(self):
        response = requests.get(f'http://127.0.0.1:{TestEndpoint.test_port}/threads')
        self.assertEqual(response.status_code, 200)

    def test_process_endpoint(self):
        response = requests.get(f'http://127.0.0.1:{TestEndpoint.test_port}/process')
        self.assertEqual(response.status_code, 200)

    def test_memory_endpoint(self):
        # Get memory map
        maps_response = requests.get(f'http://127.0.0.1:{TestEndpoint.test_port}/maps')
        self.assertEqual(maps_response.status_code, 200)

        # Parse the maps response to find a valid memory address
        lines = maps_response.text.split('\n')
        address = None
        for line in lines:
            if line:
                parts = line.split(' ')
                addr_range = parts[0].split('-')
                if len(addr_range) == 2:
                    address = addr_range[0]  # Take the start of the range
                    break

        self.assertIsNotNone(address, "No valid address found in memory maps")

        # Read from the memory address
        read_length = 256
        memory_response = requests.get(f'http://127.0.0.1:{TestEndpoint.test_port}/memory/{address}:{read_length:x}')
        self.assertEqual(memory_response.status_code, 200)
        # Add further checks here if necessary, e.g., length of response data

        # Check if the response length matches the requested length
        self.assertEqual(len(memory_response.content), read_length)

    def test_mmap_endpoint(self):
        # Request to mmap endpoint
        response = requests.get(f'http://127.0.0.1:{TestEndpoint.test_port}/mmap/0:1000:rw-')
        self.assertEqual(response.status_code, 200)

        # Check if response looks like a memory address
        address_pattern = re.compile(r'^0x[0-9a-fA-F]+$')
        self.assertIsNotNone(address_pattern.match(response.text.strip()), "Response does not look like a memory address")


    def test_mmap_and_memory_endpoint(self):
        # Request to mmap endpoint
        mmap_response = requests.get(f'http://127.0.0.1:{TestEndpoint.test_port}/mmap/0:40000:rw-')
        self.assertEqual(mmap_response.status_code, 200)

        # Check if response looks like a memory address
        address_pattern = re.compile(r'^0x[0-9a-fA-F]+$')
        address_match = address_pattern.match(mmap_response.text.strip())
        self.assertIsNotNone(address_match, "Response does not look like a memory address")
        address = address_match.group()

        # Prepare data to write
        data_to_write = "helloworldhello!"

        # Write data to the memory address using PUT request
        put_response = requests.put(f'http://127.0.0.1:{TestEndpoint.test_port}/memory/{address}:10', data=data_to_write)
        self.assertEqual(put_response.status_code, 200)

        # Read from the memory address using GET request
        get_response = requests.get(f'http://127.0.0.1:{TestEndpoint.test_port}/memory/{address}:10')
        self.assertEqual(get_response.status_code, 200)
        
        # Verify the response contains the written data
        self.assertEqual(get_response.text.strip(), data_to_write)

    def test_dlsym_printf(self):
        # Request the address of the 'printf' symbol
        response = requests.get(f'http://127.0.0.1:{TestEndpoint.test_port}/dlsym/printf')
        self.assertEqual(response.status_code, 200)

        # Check if response looks like a memory address
        address_pattern = re.compile(r'^0x[0-9a-fA-F]+$')
        self.assertIsNotNone(address_pattern.match(response.text.strip()), "Response does not look like a memory address")

    def test_python_hello_world(self):
        # Python script to be executed
        python_script = "print('Hello, World!')"
        
        # Send the script to the /python endpoint
        response = requests.put(f'http://127.0.0.1:{TestEndpoint.test_port}/python', data=python_script)
        
        # Check that the request was successful
        self.assertEqual(response.status_code, 200)
        
        # Verify that the response contains the output of the Python script
        self.assertEqual(response.text.strip(), "Hello, World!")

        # Python script to be executed
        python_script = "print(15 + 25)"
        
        # Send the script to the /python endpoint
        response = requests.put(f'http://127.0.0.1:{TestEndpoint.test_port}/python', data=python_script)
        
        # Check that the request was successful
        self.assertEqual(response.status_code, 200)
        
        # Verify that the response contains the output of the Python script
        self.assertEqual(response.text.strip(), "40")

if __name__ == '__main__':
    unittest.main()

