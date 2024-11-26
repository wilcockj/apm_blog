package main

import (
	"encoding/json"
	"fmt"
	"html/template"
	"io"
	"io/fs"
	"log"
	"net/http"
	"os"
	"sync"
)

type Event_Data struct {
	Date           string `json:"date"`
	KeyboardEvents int    `json:"keyboard_events"`
	MouseEvents    int    `json:"mouse_events"`
}

// CircularBuffer represents a fixed-size buffer that overwrites the oldest elements when full.
type CircularBuffer[T any] struct {
	Buffer []T        `json:"buffer"`
	Cap    int        `json:"cap"`
	Start  int        `json:"start"`
	Count  int        `json:"count"`
	mutex  sync.Mutex // To make it thread-safe if needed
}

// NewCircularBuffer creates a new circular buffer with the given capacity.
func NewCircularBuffer[T any](capacity int) *CircularBuffer[T] {
	return &CircularBuffer[T]{
		Buffer: make([]T, capacity),
		Cap:    capacity,
	}
}

// Append adds a new element to the Buffer, overwriting the oldest if the Buffer is full.
func (cb *CircularBuffer[T]) Append(item T) {
	cb.mutex.Lock()
	defer cb.mutex.Unlock()

	cb.Buffer[(cb.Start+cb.Count)%cb.Cap] = item
	if cb.Count < cb.Cap {
		cb.Count++
	} else {
		cb.Start = (cb.Start + 1) % cb.Cap
	}
}

// Get returns all the elements in the Buffer in the correct order.
func (cb *CircularBuffer[T]) Get() []T {
	cb.mutex.Lock()
	defer cb.mutex.Unlock()

	elements := make([]T, cb.Count)
	for i := 0; i < cb.Count; i++ {
		elements[i] = cb.Buffer[(cb.Start+i)%cb.Cap]
	}
	return elements
}

// Save writes the circular Buffer to a file in JSON format.
func (cb *CircularBuffer[T]) Save(filename string) error {
	cb.mutex.Lock()
	defer cb.mutex.Unlock()

	file, err := os.Create(filename)
	if err != nil {
		return err
	}
	defer file.Close()

	log.Println("Saving circular Buffer to file")
	encoder := json.NewEncoder(file)
	return encoder.Encode(cb)
}

// Load reads the circular Buffer from a file in JSON format.
func (cb *CircularBuffer[T]) Load(filename string) error {
	cb.mutex.Lock()
	defer cb.mutex.Unlock()

	file, err := os.Open(filename)
	if err != nil {
		return err
	}
	defer file.Close()

	decoder := json.NewDecoder(file)
	return decoder.Decode(cb)
}

// Global circular buffer
var globalBuffer *CircularBuffer[Event_Data]

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
	globalBuffer.Append(data)

	list := globalBuffer.Get()
	fmt.Printf("Circular buffer is now %d elements long\n", len(list))
	/*
		for _, el := range list {
			fmt.Printf("date = %s, keyboard = %d, mouse = %d\n", el.Date, el.KeyboardEvents, el.MouseEvents)
		}
	*/

	globalBuffer.Save("key_mouse_events.json")
}

func homepage_handler(w http.ResponseWriter, r *http.Request) {
	// Log the request headers
	//for name, values := range r.Header {
	//for _, value := range values {
	//log.Printf("Header: %s = %s\n", name, value)
	//}
	//}

	list := globalBuffer.Get()
	fmt.Printf("Got request for main page, circ buffer is %d elements long\n", len(list))
	// Parse and execute the HTML template
	tmpl := template.Must(template.ParseFiles("templates/index.html"))
	tmpl.Execute(w, nil)
}

func get_events_handler(w http.ResponseWriter, r *http.Request) {

	gb := globalBuffer.Get()

	if r.Method != "GET" {
		return
	}
	jsonBytes, err := json.Marshal(gb)
	if err != nil {
		log.Println(err)
	}
	// Set the Content-Type header to application/json
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)

	fmt.Fprintln(w, string(jsonBytes))
}

func main() {
	globalBuffer = NewCircularBuffer[Event_Data](24 * 60 * 2)
	err := globalBuffer.Load("key_mouse_events.json")
	if err != nil {
		log.Println(err)
	}
	if err == fs.ErrNotExist {
		log.Println("JSON file does not exist yet, starting circular buffer empty")
	}

	// Set up the handler for the root path
	http.HandleFunc("/", homepage_handler)
	http.HandleFunc("/PostEvent", handler)
	http.HandleFunc("/GetEvents", get_events_handler)

	// Start the server on port 5001
	log.Println("Starting server on :5001")
	if err := http.ListenAndServe("0.0.0.0:5001", nil); err != nil {
		log.Fatalf("Error starting server: %v\n", err)
	}
}
