package main

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
)

type Event_Data struct {
	Date           string `json:"date"`
	KeyboardEvents int    `json:"keyboard_events"`
	MouseEvents    int    `json:"mouse_events"`
}

func handler(w http.ResponseWriter, r *http.Request) {
	// Log the request method and URL
	log.Printf("Request Method: %s, URL: %s\n", r.Method, r.URL.String())

	// Log the request headers
	for name, values := range r.Header {
		for _, value := range values {
			log.Printf("Header: %s = %s\n", name, value)
		}
	}

	// Log the body (if any)
	body, err := io.ReadAll(r.Body)
	if err != nil {
		log.Printf("Error reading body: %v\n", err)
	} else {
		log.Printf("Body: %s\n", string(body))
	}

	var data Event_Data
	err = json.Unmarshal(body, &data)
	if err != nil {
		log.Println(err)
		return
	}
	fmt.Println(data)

	// Send a response
	//      fmt.Fprintln(w, "Request received!")
}

func main() {
	// Set up the handler for the root path
	http.HandleFunc("/", handler)

	// Start the server on port 5001
	log.Println("Starting server on :5001")
	if err := http.ListenAndServe("0.0.0.0:5001", nil); err != nil {
		log.Fatalf("Error starting server: %v\n", err)
	}
}
