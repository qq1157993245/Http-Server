### Prerequisites: 
- Linux or Ubuntu system
- GCC or clang
- make
    
### Instructions:

1. Compile the server and create an executable called "httpserver":

        make
    
    (Optional)Clean build files:
    
        make clean

2. Run the server with a specified port and optional number of threads:
   
   `./httpserver [-t <threads>] <port>`

    - Examples:
   
        Default: 4 threads
   
            ./httpserver 3000

        Custom worker thread count
   
            ./httpserver -t 8 3000

3. You can test the server in another terminal window:

   GET:
   
            printf "GET /foo.txt HTTP/1.1\r\nRequestID: 1\r\n\r\n" | ./test_nc localhost 3000
   
   PUT:
   
            printf "PUT /foo.txt HTTP/1.1\r\nContent-Length: 12\r\n\r\nHello World!" | ./test_nc localhost 3000

4. Every request is logged to stderr in the format:
   
   `<Method>,<URI>,<StatusCode>,<RequestID or 0>`
   
    - Examples:
  
        `GET,/foo.txt,200,1`
  
        `PUT,/foo.txt,201,2`

5. You can simulate concurrent access by sending multiple requests simultaneously from different terminal windows, or using a shell script.

    - Example shell script "concurrent_test.sh":
   
            #!/bin/bash
            
            printf "GET /foo.txt HTTP/1.1\r\n\r\n" | ./cse130_nc localhost 3000 &
            printf "GET /foo.txt HTTP/1.1\r\n\r\n" | ./cse130_nc localhost 3000 &
            printf "GET /foo.txt HTTP/1.1\r\n\r\n" | ./cse130_nc localhost 3000 &
            
            wait

        Run it with:
   
            chmod +x concurrent_test.sh
      
            ./concurrent_test.sh







