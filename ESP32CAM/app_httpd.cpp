#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "image_util.h"
#include "camera_index.h"
//#include "camera_index_html.h"
#include "Arduino.h"

#include "fb_gfx.h"
#include "fd_forward.h"
//#include "dl_lib.h"
#include "fr_forward.h"

#include <mask-prediction_inferencing.h>
uint8_t *out_buf;
uint8_t *ei_buf;

static int8_t ei_activate = 0;

ei_impulse_result_t result = {0};

typedef struct
{
    size_t size;  //number of values used for filtering
    size_t index; //current value index
    size_t count; //value count
    int sum;
    int *values; //array to be filled with values
} ra_filter_t;

typedef struct
{
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static ra_filter_t ra_filter;
httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size)
{
    memset(filter, 0, sizeof(ra_filter_t));

    filter->values = (int *)malloc(sample_size * sizeof(int));
    if (!filter->values)
    {
        return NULL;
    }
    memset(filter->values, 0, sample_size * sizeof(int));

    filter->size = sample_size;
    return filter;
}

static int ra_filter_run(ra_filter_t *filter, int value)
{
    if (!filter->values)
    {
        return value;
    }
    filter->sum -= filter->values[filter->index];
    filter->values[filter->index] = value;
    filter->sum += filter->values[filter->index];
    filter->index++;
    filter->index = filter->index % filter->size;
    if (filter->count < filter->size)
    {
        filter->count++;
    }
    return filter->sum / filter->count;
}

int raw_feature_get_data(size_t offset, size_t length, float *signal_ptr)
{
    memcpy(signal_ptr, ei_buf + offset, length * sizeof(float));
    return 0;
}

void classify()
{
    ////Serial.println("Getting signal...");
    //Serial.println("Err");
    // Set up pointer to look after data, crop it and convert it to RGB888
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_WIDTH;
    signal.get_data = &raw_feature_get_data;
    static char json_response1[256];

    char *p1 = json_response1;
    
    *p1++ = '[';
    ////Serial.println("Run classifier...");
    // Feed signal to the classifier
    EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false /* debug */);

    // Returned error variable "res" while data object.array in "result"
    //ei_printf("run_classifier returned: %d\n", res);
    if (res != 0)
        return;

    // print the predictions
    //ei_printf("Predictions ");
    //ei_printf("(DSP: %d ms., Classification: %d ms., Anomaly: %d ms.)", result.timing.dsp, result.timing.classification, result.timing.anomaly);
    //ei_printf(": \n");

    // Print short form result data
    //ei_printf("[");
    if(result.classification[1].value>(result.classification[2].value-0.20000)){
      Serial.println("2");
    }
    else{
      Serial.println("0");
    }
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++)
    {
      p1+=sprintf(p1, "%.5f", result.classification[ix].value);
        
        
        
        //ei_printf("%.5f", result.classification[ix].value);
#if EI_CLASSIFIER_HAS_ANOMALY == 1
        //ei_printf(", ");
        *p1++ = ',';
#else
        if (ix != EI_CLASSIFIER_LABEL_COUNT - 1)
        {
            //ei_printf(", ");
            *p1++ = ',';
        }
#endif
    }

#if EI_CLASSIFIER_HAS_ANOMALY == 1
    //ei_printf("%.3f", result.anomaly);
#endif
    //ei_printf("]\n");
    *p1++ = ']';
    //Serial.println(json_response1);
    // human-readable predictions
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++)
    {
        //ei_printf("    %s: %.5f\n", result.classification[ix].label, result.classification[ix].value);
    }
#if EI_CLASSIFIER_HAS_ANOMALY == 1
    //ei_printf("    anomaly score: %.3f\n", result.anomaly);
#endif

    //free(ei_buf);
}

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len)
{
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if (!index)
    {
        j->len = 0;
    }
    if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK)
    {
        return 0;
    }
    j->len += len;
    return len;
}

void inference_handler()//static esp_err_t inference_handler(httpd_req_t *req) 
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    int64_t fr_start = esp_timer_get_time();

    size_t out_len, out_width, out_height;
    size_t ei_len;

    fb = esp_camera_fb_get();
    if (!fb)
    {
        //Serial.println("Camera capture failed");
        Serial.println("Err");
        //httpd_resp_send_500(req);
        return ;//ESP_FAIL;
    }

    //httpd_resp_set_type(req, "image/jpeg");
    //httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg"); //response header and capture page

    bool s;
    bool detected = false;

    dl_matrix3du_t *image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
    if (!image_matrix)
    {
        esp_camera_fb_return(fb);
        //Serial.println("dl_matrix3du_alloc failed");
        Serial.println("Err");
        //httpd_resp_send_500(req);
        return ;//ESP_FAIL;
    }

    out_buf = image_matrix->item;
    out_len = fb->width * fb->height * 3;
    out_width = fb->width;
    out_height = fb->height;

    //Serial.println("Converting to RGB888...");
    //Serial.println("Err");
    int64_t time_start = esp_timer_get_time();
    s = fmt2rgb888(fb->buf, fb->len, fb->format, out_buf);
    int64_t time_end = esp_timer_get_time();
    //Serial.printf("Done in %ums\n", (uint32_t)((time_end - time_start) / 1000));

    esp_camera_fb_return(fb);
    if (!s)
    {
        dl_matrix3du_free(image_matrix);
        //Serial.println("to rgb888 failed");
        Serial.println("Err");
        //httpd_resp_send_500(req);
        return;// ESP_FAIL;
    }

    dl_matrix3du_t *ei_matrix = dl_matrix3du_alloc(1, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, 3);
    if (!ei_matrix)
    {
        esp_camera_fb_return(fb);
        //Serial.println("dl_matrix3du_alloc failed");
        Serial.println("Err");
        //httpd_resp_send_500(req);
        return;// ESP_FAIL;
    }

    ei_buf = ei_matrix->item;
    ei_len = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT * 3;

    //Serial.println("Resizing the frame buffer...");
    time_start = esp_timer_get_time();
    image_resize_linear(ei_buf, out_buf, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, 3, out_width, out_height);
    time_end = esp_timer_get_time();
    //Serial.printf("Done in %ums\n", (uint32_t)((time_end - time_start) / 1000));

    dl_matrix3du_free(image_matrix);

    classify();

    //jpg_chunking_t jchunk = {req, 0};
    //s = fmt2jpg_cb(ei_buf, ei_len, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, PIXFORMAT_RGB888, 90, jpg_encode_stream, &jchunk);

    // if (!s)
    // {
    //     //Serial.println("JPEG compression failed");
    //     Serial.println("Err");
    //     return ESP_FAIL;
    // }

    dl_matrix3du_free(ei_matrix);

    int64_t fr_end = esp_timer_get_time();
    //Serial.printf("JPG: %uB %ums\n", (uint32_t)(jchunk.len), (uint32_t)((fr_end - fr_start) / 1000));
    return ;//res;
}

void startCameraServer()
{
    inference_handler();
    inference_handler();
    inference_handler();
    inference_handler();
    
}
