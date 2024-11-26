#include "find_event_file.h"
#include <assert.h>
#include <curl/curl.h>
#include <poll.h>
#include <stdint.h>
#include <time.h>

#define NUM_KEYCODES 71
#define REPORT_INTERVAL 30 * 1000
#define MOUSE_MOUSE_INTERVAL 50
#define POLL_INTERVAL_MS 200

#define LOG(fmt, ...)                                                          \
  do {                                                                         \
    if (debug)                                                                 \
      printf(fmt, ##__VA_ARGS__);                                              \
  } while (0)

uint64_t get_current_timestamp_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

char debug = 1;
char *url = NULL;
int report_interval = 30 * 1000;

void send_data_to_backend(CURL *curl, char *date, uint32_t keyboard_events,
                          uint32_t mouse_events) {
  CURLcode res;
  /* First set the URL that is about to receive our POST. This URL can
   just as well be an https:// URL if that is what should receive the
   data. */
  char url_buf[100];
  strncpy(url_buf, url, 100);
  char *endpoint = "/PostEvent";
  strncat(url_buf, endpoint, 100);

  curl_easy_setopt(curl, CURLOPT_URL, url_buf);
  char post_buf[200];
  snprintf(post_buf, 200,
           "{\"date\":\"%s\",\"keyboard_events\":%d,\"mouse_events\":%d}", date,
           keyboard_events, mouse_events);
  /* Now specify the POST data */
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_buf);

  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  /* Perform the request, res gets the return code */
  res = curl_easy_perform(curl);
  /* Check for errors */
  if (res != CURLE_OK) {
    fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
    return;
  }

  // check response code
  long response_code;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
  if (response_code != 200) {
    printf("\nPOST Failed\nResponse Code: %ld\n", response_code);
    return;
  }

  LOG("Successfully posted data to endpoint %s\n", url);
}

int main(int argc, char *argv[]) {

  for (int i = 1; i < argc; i++) {
    if (strcmp("--url", argv[i]) == 0) {
      assert(argc - 1 > i && "Don't have data following --url");
      i++;
      LOG("Got url %s\n", argv[i]);
      url = argv[i];
    }

    if (strcmp("--interval", argv[i]) == 0) {
      assert(argc - 1 > i && "Don't have data following --interval");
      i++;
      LOG("Got interval %s\n", argv[i]);
      report_interval = 1000 * atoi(argv[i]);
    }
  }

  assert(url != NULL);

  CURL *curl;
  CURLcode res;

  /* In Windows, this inits the Winsock stuff */
  curl_global_init(CURL_GLOBAL_ALL);

  /* get a curl handle */
  curl = curl_easy_init();

  char *keyboard_file = get_keyboard_event_file();
  int keyboard_fd;
  uint64_t last_report = get_current_timestamp_ms();

  printf("Opening keyboard file %s\n", keyboard_file);
  if ((keyboard_fd = open(keyboard_file, O_RDONLY)) < 0) {
    printf(
        "Error accessing keyboard from %s. May require you to be superuser\n",
        keyboard_file);
    return 1;
  }

  int opts = fcntl(keyboard_fd, F_GETFL);
  if (opts < 0) {
    perror("fnctl(F_GETFL)");
    exit(EXIT_FAILURE);
  }

  opts = (opts | O_NONBLOCK);
  if (fcntl(keyboard_fd, F_SETFL, opts) < 0) {
    perror("fnctl(F_SETFL)");
    exit(EXIT_FAILURE);
  }

  // temp mouse file
  int mouse_fd = open("/dev/input/event2", O_RDONLY);

  opts = fcntl(keyboard_fd, F_GETFL);
  if (opts < 0) {
    perror("fnctl(F_GETFL)");
    exit(EXIT_FAILURE);
  }

  opts = (opts | O_NONBLOCK);
  if (fcntl(mouse_fd, F_SETFL, opts) < 0) {
    perror("fnctl(F_SETFL)");
    exit(EXIT_FAILURE);
  }

  FILE *data_file;
  // Open data file for writing
  data_file = fopen("resources/keyboard_activity.dat", "a");
  if (data_file == NULL) {
    perror("Error opening data file");
    return 1;
  }

  // Write header for gnuplot
  // fprintf(data_file, "# Timestamp Events\n");
  // fprintf(data_file, "# Time format: YYYY-MM-DD HH:MM:SS\n");

  // i want to select for some amount of time e.g. a second
  // after which point i read some amount of the file to
  // find out how many actions have happened

  struct input_event event;
  int keyboard_events = 0;
  int mouse_events = 0;

  uint64_t last_mouse_move = get_current_timestamp_ms();

  while (1) {
    // Use poll instead of select for better efficiency
    struct pollfd fds[2];
    fds[0].fd = keyboard_fd;
    fds[0].events = POLLIN;
    fds[1].fd = mouse_fd;
    fds[1].events = POLLIN;

    uint64_t ms_until_report =
        report_interval - (get_current_timestamp_ms() - last_report);
    uint64_t poll_interval =
        ms_until_report < POLL_INTERVAL_MS ? ms_until_report : POLL_INTERVAL_MS;

    // poll for either the POLL_INTERVAL_MS or time till report, whichever is
    // less
    int ret = poll(fds, 2, poll_interval);

    if (ret > 0) {
      // Handle keyboard events
      if (fds[0].revents & POLLIN) {
        while (read(keyboard_fd, &event, sizeof(event)) > 0) {
          if (event.type == EV_KEY && event.value == 1 && event.code > 0 &&
              event.code < NUM_KEYCODES) {
            keyboard_events++;
          }
        }
      }

      // Handle mouse events
      if (fds[1].revents & POLLIN) {
        while (read(mouse_fd, &event, sizeof(event)) > 0) {
          if (event.type == EV_KEY && (event.value == 1 || event.value == 0)) {
            mouse_events++; // Mouse button press/release
          } else if (event.type == EV_REL || event.type == EV_ABS) {
            uint64_t current_mouse = get_current_timestamp_ms();
            if (current_mouse - last_mouse_move > MOUSE_MOUSE_INTERVAL) {
              last_mouse_move = get_current_timestamp_ms();
              mouse_events++; // Mouse movement
            }
          }
        }
      }
    }

    // Check if it's time to report
    uint64_t current = get_current_timestamp_ms();
    char timestamp[64];
    struct tm *tm_info;
    if (current - last_report >= report_interval) {

      time_t current_time = time(NULL);
      tm_info = localtime(&current_time);
      strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
      fprintf(data_file, "%s %d\n", timestamp, keyboard_events);
      fflush(data_file); // Ensure data is written immediately

      // POST data to backend
      uint64_t start = get_current_timestamp_ms();
      send_data_to_backend(curl, timestamp, keyboard_events, mouse_events);
      printf("Took %ldms to send to backend\n",
             get_current_timestamp_ms() - start);

      printf("Logged: %s - %d keyboard_events %d mouse_events\n", timestamp,
             keyboard_events, mouse_events);
      keyboard_events = 0;
      mouse_events = 0;
      last_report = current;
    }
  }
  fclose(data_file);
  close(keyboard_fd);
  curl_global_cleanup();
  curl_easy_cleanup(curl);
  return EXIT_SUCCESS;
}
