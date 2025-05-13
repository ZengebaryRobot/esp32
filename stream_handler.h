#ifndef STREAM_HANDLER_H
#define STREAM_HANDLER_H

#ifdef __cplusplus
extern "C"
{
#endif

  // Function to initialize and start the streaming server
  bool start_stream_server(void);

  // Function to stop the streaming server
  void stop_stream_server(void);

#ifdef __cplusplus
}
#endif

#endif // STREAM_HANDLER_H
