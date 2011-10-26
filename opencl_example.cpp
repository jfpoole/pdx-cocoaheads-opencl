/*
  This is free and unencumbered software released into the public domain.

  Anyone is free to copy, modify, publish, use, compile, sell, or
  distribute this software, either in source code form or as a compiled
  binary, for any purpose, commercial or non-commercial, and by any
  means.

  In jurisdictions that recognize copyright laws, the author or authors
  of this software dedicate any and all copyright interest in the
  software to the public domain. We make this dedication for the benefit
  of the public at large and to the detriment of our heirs and
  successors. We intend this dedication to be an overt act of
  relinquishment in perpetuity of all present and future rights to this
  software under copyright law.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
  OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
  OTHER DEALINGS IN THE SOFTWARE.

  For more information, please refer to <http://unlicense.org/>
*/

/*
  Built with:
  
    g++ opencl_example.cpp timer.cpp -o opencl_example -framework OpenCL
    
  Run with:
  
    ./opencl_example --use-cpu
    ./opencl_example --use-gpu
*/

#include <fstream>
#include <getopt.h>
#include <iostream>
#include <stdexcept>
#include <OpenCL/opencl.h>

#include "timer.h"

float add_opencl(bool use_gpu, int* c_host, int* a_host, int* b_host, int N)
{
  // Create a context.
  cl_device_id device_id;
  
  cl_int err = clGetDeviceIDs(NULL, use_gpu ? CL_DEVICE_TYPE_GPU : CL_DEVICE_TYPE_CPU, 1, &device_id, NULL);
  if (err != CL_SUCCESS) {
    throw std::runtime_error("clGetDeviceIDs");
  }

  cl_context context = clCreateContext(0, 1, &device_id, NULL, NULL, &err);
  if (!context) {
    throw std::runtime_error("clCreateContext");
  }

  // Create a command queue.
  cl_command_queue commands = clCreateCommandQueue(context, device_id, 0, &err);
  if (!commands) {
    throw std::runtime_error("clCreateCommandQueue");
  }
  
  // Load the source code from the file.
  std::ifstream source_file("opencl_example.cl");    
  std::string source((std::istreambuf_iterator<char>(source_file)), std::istreambuf_iterator<char>());
  const char* source_cstr = source.c_str();

  // Create a program from the source buffer.
  cl_program program = clCreateProgramWithSource(context, 1, (const char **)&source_cstr, NULL, &err);
  if (!program) {
    throw std::runtime_error("clCreateProgramWithSource");
  }
  
  // Build the program.
  err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
  if (err != CL_SUCCESS) {
    throw std::runtime_error("clBuildProgram");
  }

  // Extract the compute kernel from the program.
  cl_kernel kernel = clCreateKernel(program, "add", &err);
  if (!kernel || err != CL_SUCCESS) {
    throw std::runtime_error("clCreateKernel");
  }
  
  // Create the device buffers for our kernel (two inputs, one output).
  cl_mem a_device = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(int) * N, NULL, NULL);
  cl_mem b_device = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(int) * N, NULL, NULL);
  cl_mem c_device = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(int) * N, NULL, NULL);
  if (!a_device || !b_device || !c_device) {
    throw std::runtime_error("clCreateBuffer");
  }
  
  // Write the input arrays into device memory.
  err = clEnqueueWriteBuffer(commands, a_device, CL_TRUE, 0, sizeof(int) * N, a_host, 0, NULL, NULL);
  if (err != CL_SUCCESS) {
    throw std::runtime_error("clEnqueueWriteBuffer");
  }

  err = clEnqueueWriteBuffer(commands, b_device, CL_TRUE, 0, sizeof(int) * N, b_host, 0, NULL, NULL);
  if (err != CL_SUCCESS) {
    throw std::runtime_error("clEnqueueWriteBuffer");
  }

  // Set the arguments to the kernel
  err = 0;
  err  = clSetKernelArg(kernel, 0, sizeof(cl_mem), &c_device);
  err |= clSetKernelArg(kernel, 1, sizeof(cl_mem), &a_device);
  err |= clSetKernelArg(kernel, 2, sizeof(cl_mem), &b_device);
  if (err != CL_SUCCESS) {
    throw std::runtime_error("clSetKernelArg");
  }

  // Get the maximum work group size for the device we're using.
  size_t local_size = 0;
  err = clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local_size), &local_size, NULL);
  if (err != CL_SUCCESS) {
    throw std::runtime_error("clGetKernelWorkGroupInfo");
  }

  Timer execution_timer;
  execution_timer.start();
  {{
    // Execute the kernel over the entire arrays using the maximum number
    // of work group items (i.e., local_size) for this device.
    size_t global_size = N;
    err = clEnqueueNDRangeKernel(commands, kernel, 1, NULL, &global_size, &local_size, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
      throw std::runtime_error("clEnqueueNDRangeKernel");
    }

    // Wait for the command commands to get serviced before reading back results
    clFinish(commands);
  }}
  execution_timer.stop();
  
  // Read the output array from device memory into host memory.
  err = clEnqueueReadBuffer(commands, c_device, CL_TRUE, 0, sizeof(int) * N, c_host, 0, NULL, NULL);  
  if (err != CL_SUCCESS) {
    throw std::runtime_error("clEnqueueReadBuffer");
  }
  
  // Validate the output array.
  for (size_t i = 0; i < N; i++) {
    if (c_host[i] != a_host[i] + b_host[i]) {
      throw std::runtime_error("Result validation failed");
    }
  }
  
  clReleaseMemObject(a_device);
  clReleaseMemObject(b_device);
  clReleaseMemObject(c_device);
  clReleaseProgram(program);
  clReleaseKernel(kernel);
  clReleaseCommandQueue(commands);
  clReleaseContext(context);
  
  return execution_timer.elapsed();
}

int main(int argc, char** argv)
{
  int o = 0;
  bool use_gpu = false;
  
  struct option longopts[] = {
    { "use-cpu", no_argument, 0, 'c' },
    { "use-gpu", no_argument, 0, 'g' },
  };
  
  while((o = getopt_long(argc, argv, "cg", longopts, 0)) != -1) {
    switch(o) {
      case 'c':
        use_gpu = false;
        break;
      case 'g':
        use_gpu = true;
        break;
      default:
        break;
    }
  }
  
  // Allocate and initialize the data sets for the kernel.
  unsigned int N = 32 * 1024 * 1024;
  int* a = new int[N];
  int* b = new int[N];
  int* c = new int[N];
  for (size_t i = 0; i < N; i++) {
    a[i] = i;
    b[i] = 2 * i;
  }
  
  double opencl_time = add_opencl(use_gpu, c, b, a, N);
  std::cout << "OpenCL execution time: " << opencl_time << "s" << std::endl;
  
  delete [] c;
  delete [] b;
  delete [] a;
  
  return 0;
}
