#include "stream_handler.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "esp_timer.h"
#include "Arduino.h"

// Define streaming constants
#define PART_BOUNDARY "123456789000000000000987654321"
#define _STREAM_CONTENT_TYPE "multipart/x-mixed-replace;boundary=" PART_BOUNDARY
#define _STREAM_BOUNDARY "\r\n--" PART_BOUNDARY "\r\n"
#define _STREAM_PART "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n"

// Running average filter for frame rate calculation
typedef struct {
    uint32_t size;
    uint32_t index;
    uint32_t count;
    uint32_t total;
    uint32_t *values;
} ra_filter_t;

static ra_filter_t ra_filter;
static httpd_handle_t stream_httpd = NULL;

static uint32_t ra_filter_run(ra_filter_t *filter, uint32_t value) {
    if (!filter->values) {
        return value;
    }
    filter->total -= filter->values[filter->index];
    filter->values[filter->index] = value;
    filter->total += filter->values[filter->index];
    filter->index = (filter->index + 1) % filter->size;
    if (filter->count < filter->size) {
        filter->count++;
    }
    return filter->total / filter->count;
}

static esp_err_t ra_filter_init(ra_filter_t *filter, size_t size) {
    filter->size = size;
    filter->index = 0;
    filter->count = 0;
    filter->total = 0;
    filter->values = (uint32_t *)calloc(size, sizeof(uint32_t));
    if (!filter->values) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// Streaming handler function for the ESP-IDF HTTP server
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  struct timeval _timestamp;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char *part_buf[128];

  static int64_t last_frame = 0;
  if (!last_frame) {
    last_frame = esp_timer_get_time();
  }

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", "60");

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      _timestamp.tv_sec = fb->timestamp.tv_sec;
      _timestamp.tv_usec = fb->timestamp.tv_usec;
      if (fb->format != PIXFORMAT_JPEG) {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if (!jpeg_converted) {
          Serial.println("JPEG compression failed");
          res = ESP_FAIL;
        }
      } else {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) {
      Serial.println("Send frame failed");
      break;
    }
    int64_t fr_end = esp_timer_get_time();

    int64_t frame_time = fr_end - last_frame;
    last_frame = fr_end;

    frame_time /= 1000;
    
    uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
    Serial.printf("MJPG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps)\n", 
      (uint32_t)(_jpg_buf_len), 
      (uint32_t)frame_time, 
      1000.0 / (uint32_t)frame_time, 
      avg_frame_time,
      1000.0 / avg_frame_time
    );
  }

  return res;
}

// Public function to start streaming server
bool start_stream_server(void) {
  if (stream_httpd != NULL) {
    return true; // Server already running
  }

  // Initialize the running average filter
  ra_filter_init(&ra_filter, 32);

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 81; // Use port 81 to avoid conflict with AsyncWebServer

  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
  };

  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    Serial.println("HTTP stream server started on port 81");
    return true;
  }
  
  Serial.println("Failed to start HTTP stream server");
  return false;
}

// Public function to stop streaming server
void stop_stream_server(void) {
  if (stream_httpd != NULL) {
    httpd_stop(stream_httpd);
    stream_httpd = NULL;
    Serial.println("HTTP stream server stopped");
  }
  
  // Free filter resources
  if (ra_filter.values) {
    free(ra_filter.values);
    ra_filter.values = NULL;
  }
}
