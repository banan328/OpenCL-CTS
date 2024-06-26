//
// Copyright (c) 2023 The Khronos Group Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//


#include "harness/typeWrappers.h"
#include "harness/extensionHelpers.h"
#include "harness/errorHelpers.h"
#include <system_error>
#include <thread>
#include <chrono>
#include <vector>

#define FLUSH_DELAY_S 5

#define SEMAPHORE_PARAM_TEST(param_name, param_type, expected)                 \
    do                                                                         \
    {                                                                          \
        param_type value;                                                      \
        size_t size;                                                           \
        cl_int error = clGetSemaphoreInfoKHR(sema, param_name, sizeof(value),  \
                                             &value, &size);                   \
        test_error(error, "Unable to get " #param_name " from semaphore");     \
        if (value != expected)                                                 \
        {                                                                      \
            test_fail("ERROR: Parameter %s did not validate! (expected %d, "   \
                      "got %d)\n",                                             \
                      #param_name, expected, value);                           \
        }                                                                      \
        if (size != sizeof(value))                                             \
        {                                                                      \
            test_fail(                                                         \
                "ERROR: Returned size of parameter %s does not validate! "     \
                "(expected %d, got %d)\n",                                     \
                #param_name, (int)sizeof(value), (int)size);                   \
        }                                                                      \
    } while (false)

#define SEMAPHORE_PARAM_TEST_ARRAY(param_name, param_type, num_params,         \
                                   expected)                                   \
    do                                                                         \
    {                                                                          \
        param_type value[num_params];                                          \
        size_t size;                                                           \
        cl_int error = clGetSemaphoreInfoKHR(sema, param_name, sizeof(value),  \
                                             &value, &size);                   \
        test_error(error, "Unable to get " #param_name " from semaphore");     \
        if (size != sizeof(value))                                             \
        {                                                                      \
            test_fail(                                                         \
                "ERROR: Returned size of parameter %s does not validate! "     \
                "(expected %d, got %d)\n",                                     \
                #param_name, (int)sizeof(value), (int)size);                   \
        }                                                                      \
        if (memcmp(value, expected, size) != 0)                                \
        {                                                                      \
            test_fail("ERROR: Parameter %s did not validate!\n", #param_name); \
        }                                                                      \
    } while (false)

static const char* source = "__kernel void empty() {}";

// Helper function that signals and waits on semaphore across two different
// queues.
static int semaphore_cross_queue_helper(cl_device_id deviceID,
                                        cl_context context,
                                        cl_command_queue queue_1,
                                        cl_command_queue queue_2)
{
    cl_int err;

    if (!is_extension_available(deviceID, "cl_khr_semaphore"))
    {
        log_info("cl_khr_semaphore is not supported on this platform. "
                 "Skipping test.\n");
        return TEST_SKIPPED_ITSELF;
    }

    // Obtain pointers to semaphore's API
    GET_PFN(deviceID, clCreateSemaphoreWithPropertiesKHR);
    GET_PFN(deviceID, clEnqueueSignalSemaphoresKHR);
    GET_PFN(deviceID, clEnqueueWaitSemaphoresKHR);
    GET_PFN(deviceID, clReleaseSemaphoreKHR);

    // Create semaphore
    cl_semaphore_properties_khr sema_props[] = {
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_KHR),
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_BINARY_KHR),
        0
    };
    cl_semaphore_khr sema =
        clCreateSemaphoreWithPropertiesKHR(context, sema_props, &err);
    test_error(err, "Could not create semaphore");

    // Signal semaphore on queue_1
    clEventWrapper signal_event;
    err = clEnqueueSignalSemaphoresKHR(queue_1, 1, &sema, nullptr, 0, nullptr,
                                       &signal_event);
    test_error(err, "Could not signal semaphore");

    // Wait semaphore on queue_2
    clEventWrapper wait_event;
    err = clEnqueueWaitSemaphoresKHR(queue_2, 1, &sema, nullptr, 0, nullptr,
                                     &wait_event);
    test_error(err, "Could not wait semaphore");

    // Finish queue_1 and queue_2
    err = clFinish(queue_1);
    test_error(err, "Could not finish queue");

    err = clFinish(queue_2);
    test_error(err, "Could not finish queue");

    // Ensure all events are completed
    test_assert_event_complete(signal_event);
    test_assert_event_complete(wait_event);

    // Release semaphore
    err = clReleaseSemaphoreKHR(sema);
    test_error(err, "Could not release semaphore");

    return TEST_PASS;
}

// Confirm that a signal followed by a wait will complete successfully
int test_semaphores_simple_1(cl_device_id deviceID, cl_context context,
                             cl_command_queue defaultQueue, int num_elements)
{
    cl_int err;

    if (!is_extension_available(deviceID, "cl_khr_semaphore"))
    {
        log_info("cl_khr_semaphore is not supported on this platform. "
                 "Skipping test.\n");
        return TEST_SKIPPED_ITSELF;
    }

    // Obtain pointers to semaphore's API
    GET_PFN(deviceID, clCreateSemaphoreWithPropertiesKHR);
    GET_PFN(deviceID, clEnqueueSignalSemaphoresKHR);
    GET_PFN(deviceID, clEnqueueWaitSemaphoresKHR);
    GET_PFN(deviceID, clReleaseSemaphoreKHR);

    // Create ooo queue
    clCommandQueueWrapper queue = clCreateCommandQueue(
        context, deviceID, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &err);
    test_error(err, "Could not create command queue");

    // Create semaphore
    cl_semaphore_properties_khr sema_props[] = {
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_KHR),
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_BINARY_KHR),
        0
    };
    cl_semaphore_khr sema =
        clCreateSemaphoreWithPropertiesKHR(context, sema_props, &err);
    test_error(err, "Could not create semaphore");

    // Signal semaphore
    clEventWrapper signal_event;
    err = clEnqueueSignalSemaphoresKHR(queue, 1, &sema, nullptr, 0, nullptr,
                                       &signal_event);
    test_error(err, "Could not signal semaphore");

    // Wait semaphore
    clEventWrapper wait_event;
    err = clEnqueueWaitSemaphoresKHR(queue, 1, &sema, nullptr, 0, nullptr,
                                     &wait_event);
    test_error(err, "Could not wait semaphore");

    // Finish
    err = clFinish(queue);
    test_error(err, "Could not finish queue");

    // Ensure all events are completed
    test_assert_event_complete(signal_event);
    test_assert_event_complete(wait_event);

    // Release semaphore
    err = clReleaseSemaphoreKHR(sema);
    test_error(err, "Could not release semaphore");

    return TEST_PASS;
}

// Confirm that signal a semaphore with no event dependencies will not result
// in an implicit dependency on everything previously submitted
int test_semaphores_simple_2(cl_device_id deviceID, cl_context context,
                             cl_command_queue defaultQueue, int num_elements)
{
    cl_int err;

    if (!is_extension_available(deviceID, "cl_khr_semaphore"))
    {
        log_info("cl_khr_semaphore is not supported on this platform. "
                 "Skipping test.\n");
        return TEST_SKIPPED_ITSELF;
    }

    // Obtain pointers to semaphore's API
    GET_PFN(deviceID, clCreateSemaphoreWithPropertiesKHR);
    GET_PFN(deviceID, clEnqueueSignalSemaphoresKHR);
    GET_PFN(deviceID, clEnqueueWaitSemaphoresKHR);
    GET_PFN(deviceID, clReleaseSemaphoreKHR);

    // Create ooo queue
    clCommandQueueWrapper queue = clCreateCommandQueue(
        context, deviceID, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &err);
    test_error(err, "Could not create command queue");

    // Create semaphore
    cl_semaphore_properties_khr sema_props[] = {
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_KHR),
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_BINARY_KHR),
        0
    };
    cl_semaphore_khr sema =
        clCreateSemaphoreWithPropertiesKHR(context, sema_props, &err);
    test_error(err, "Could not create semaphore");

    // Create user event
    clEventWrapper user_event = clCreateUserEvent(context, &err);
    test_error(err, "Could not create user event");

    // Create Kernel
    clProgramWrapper program;
    clKernelWrapper kernel;
    err = create_single_kernel_helper(context, &program, &kernel, 1, &source,
                                      "empty");
    test_error(err, "Could not create kernel");

    // Enqueue task_1 (dependency on user_event)
    clEventWrapper task_1_event;
    err = clEnqueueTask(queue, kernel, 1, &user_event, &task_1_event);
    test_error(err, "Could not enqueue task 1");

    // Signal semaphore
    clEventWrapper signal_event;
    err = clEnqueueSignalSemaphoresKHR(queue, 1, &sema, nullptr, 0, nullptr,
                                       &signal_event);
    test_error(err, "Could not signal semaphore");

    // Wait semaphore
    clEventWrapper wait_event;
    err = clEnqueueWaitSemaphoresKHR(queue, 1, &sema, nullptr, 0, nullptr,
                                     &wait_event);
    test_error(err, "Could not wait semaphore");

    // Flush and delay
    err = clFlush(queue);
    test_error(err, "Could not flush queue");
    std::this_thread::sleep_for(std::chrono::seconds(FLUSH_DELAY_S));

    // Ensure all events are completed except for task_1
    test_assert_event_inprogress(task_1_event);
    test_assert_event_complete(signal_event);
    test_assert_event_complete(wait_event);

    // Complete user_event
    err = clSetUserEventStatus(user_event, CL_COMPLETE);
    test_error(err, "Could not set user event to CL_COMPLETE");

    // Finish
    err = clFinish(queue);
    test_error(err, "Could not finish queue");

    // Ensure all events are completed
    test_assert_event_complete(task_1_event);
    test_assert_event_complete(signal_event);
    test_assert_event_complete(wait_event);

    // Release semaphore
    err = clReleaseSemaphoreKHR(sema);
    test_error(err, "Could not release semaphore");

    return TEST_PASS;
}

// Confirm that a semaphore can be reused multiple times
int test_semaphores_reuse(cl_device_id deviceID, cl_context context,
                          cl_command_queue defaultQueue, int num_elements)
{
    cl_int err;

    if (!is_extension_available(deviceID, "cl_khr_semaphore"))
    {
        log_info("cl_khr_semaphore is not supported on this platform. "
                 "Skipping test.\n");
        return TEST_SKIPPED_ITSELF;
    }

    // Obtain pointers to semaphore's API
    GET_PFN(deviceID, clCreateSemaphoreWithPropertiesKHR);
    GET_PFN(deviceID, clEnqueueSignalSemaphoresKHR);
    GET_PFN(deviceID, clEnqueueWaitSemaphoresKHR);
    GET_PFN(deviceID, clReleaseSemaphoreKHR);

    // Create ooo queue
    clCommandQueueWrapper queue = clCreateCommandQueue(
        context, deviceID, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &err);
    test_error(err, "Could not create command queue");

    // Create semaphore
    cl_semaphore_properties_khr sema_props[] = {
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_KHR),
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_BINARY_KHR),
        0
    };
    cl_semaphore_khr sema =
        clCreateSemaphoreWithPropertiesKHR(context, sema_props, &err);
    test_error(err, "Could not create semaphore");

    // Create Kernel
    clProgramWrapper program;
    clKernelWrapper kernel;
    err = create_single_kernel_helper(context, &program, &kernel, 1, &source,
                                      "empty");
    test_error(err, "Could not create kernel");

    constexpr size_t loop_count = 10;
    clEventWrapper signal_events[loop_count];
    clEventWrapper wait_events[loop_count];
    clEventWrapper task_events[loop_count];

    // Enqueue task_1
    err = clEnqueueTask(queue, kernel, 0, nullptr, &task_events[0]);
    test_error(err, "Unable to enqueue task_1");

    // Signal semaphore (dependency on task_1)
    err = clEnqueueSignalSemaphoresKHR(queue, 1, &sema, nullptr, 1,
                                       &task_events[0], &signal_events[0]);
    test_error(err, "Could not signal semaphore");

    // In a loop
    size_t loop;
    for (loop = 1; loop < loop_count; ++loop)
    {
        // Wait semaphore
        err = clEnqueueWaitSemaphoresKHR(queue, 1, &sema, nullptr, 0, nullptr,
                                         &wait_events[loop - 1]);
        test_error(err, "Could not wait semaphore");

        // Enqueue task_loop (dependency on wait)
        err = clEnqueueTask(queue, kernel, 1, &wait_events[loop - 1],
                            &task_events[loop]);
        test_error(err, "Unable to enqueue task_loop");

        // Wait for the "wait semaphore" to complete
        err = clWaitForEvents(1, &wait_events[loop - 1]);
        test_error(err, "Unable to wait for wait semaphore to complete");

        // Signal semaphore (dependency on task_loop)
        err = clEnqueueSignalSemaphoresKHR(queue, 1, &sema, nullptr, 1,
                                           &task_events[loop],
                                           &signal_events[loop]);
        test_error(err, "Could not signal semaphore");
    }

    // Wait semaphore
    err = clEnqueueWaitSemaphoresKHR(queue, 1, &sema, nullptr, 0, nullptr,
                                     &wait_events[loop - 1]);
    test_error(err, "Could not wait semaphore");

    // Finish
    err = clFinish(queue);
    test_error(err, "Could not finish queue");

    // Ensure all events are completed
    for (loop = 0; loop < loop_count; ++loop)
    {
        test_assert_event_complete(wait_events[loop]);
        test_assert_event_complete(signal_events[loop]);
        test_assert_event_complete(task_events[loop]);
    }

    // Release semaphore
    err = clReleaseSemaphoreKHR(sema);
    test_error(err, "Could not release semaphore");

    return TEST_PASS;
}

// Confirm that a semaphore works across different ooo queues
int test_semaphores_cross_queues_ooo(cl_device_id deviceID, cl_context context,
                                     cl_command_queue defaultQueue,
                                     int num_elements)
{
    cl_int err;

    // Create ooo queues
    clCommandQueueWrapper queue_1 = clCreateCommandQueue(
        context, deviceID, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &err);
    test_error(err, "Could not create command queue");

    clCommandQueueWrapper queue_2 = clCreateCommandQueue(
        context, deviceID, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &err);
    test_error(err, "Could not create command queue");

    return semaphore_cross_queue_helper(deviceID, context, queue_1, queue_2);
}

// Confirm that a semaphore works across different in-order queues
int test_semaphores_cross_queues_io(cl_device_id deviceID, cl_context context,
                                    cl_command_queue defaultQueue,
                                    int num_elements)
{
    cl_int err;

    // Create in-order queues
    clCommandQueueWrapper queue_1 =
        clCreateCommandQueue(context, deviceID, 0, &err);
    test_error(err, "Could not create command queue");

    clCommandQueueWrapper queue_2 =
        clCreateCommandQueue(context, deviceID, 0, &err);
    test_error(err, "Could not create command queue");

    return semaphore_cross_queue_helper(deviceID, context, queue_1, queue_2);
}

// Confirm that we can signal multiple semaphores with one command
int test_semaphores_multi_signal(cl_device_id deviceID, cl_context context,
                                 cl_command_queue defaultQueue,
                                 int num_elements)
{
    cl_int err;

    if (!is_extension_available(deviceID, "cl_khr_semaphore"))
    {
        log_info("cl_khr_semaphore is not supported on this platform. "
                 "Skipping test.\n");
        return TEST_SKIPPED_ITSELF;
    }

    // Obtain pointers to semaphore's API
    GET_PFN(deviceID, clCreateSemaphoreWithPropertiesKHR);
    GET_PFN(deviceID, clEnqueueSignalSemaphoresKHR);
    GET_PFN(deviceID, clEnqueueWaitSemaphoresKHR);
    GET_PFN(deviceID, clReleaseSemaphoreKHR);

    // Create ooo queue
    clCommandQueueWrapper queue = clCreateCommandQueue(
        context, deviceID, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &err);
    test_error(err, "Could not create command queue");

    // Create semaphore
    cl_semaphore_properties_khr sema_props[] = {
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_KHR),
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_BINARY_KHR),
        0
    };
    cl_semaphore_khr sema_1 =
        clCreateSemaphoreWithPropertiesKHR(context, sema_props, &err);
    test_error(err, "Could not create semaphore");

    cl_semaphore_khr sema_2 =
        clCreateSemaphoreWithPropertiesKHR(context, sema_props, &err);
    test_error(err, "Could not create semaphore");

    // Signal semaphore 1 and 2
    clEventWrapper signal_event;
    cl_semaphore_khr sema_list[] = { sema_1, sema_2 };
    err = clEnqueueSignalSemaphoresKHR(queue, 2, sema_list, nullptr, 0, nullptr,
                                       &signal_event);
    test_error(err, "Could not signal semaphore");

    // Wait semaphore 1
    clEventWrapper wait_1_event;
    err = clEnqueueWaitSemaphoresKHR(queue, 1, &sema_1, nullptr, 0, nullptr,
                                     &wait_1_event);
    test_error(err, "Could not wait semaphore");

    // Wait semaphore 2
    clEventWrapper wait_2_event;
    err = clEnqueueWaitSemaphoresKHR(queue, 1, &sema_2, nullptr, 0, nullptr,
                                     &wait_2_event);
    test_error(err, "Could not wait semaphore");

    // Finish
    err = clFinish(queue);
    test_error(err, "Could not finish queue");

    // Ensure all events are completed
    test_assert_event_complete(signal_event);
    test_assert_event_complete(wait_1_event);
    test_assert_event_complete(wait_2_event);

    // Release semaphores
    err = clReleaseSemaphoreKHR(sema_1);
    test_error(err, "Could not release semaphore");

    err = clReleaseSemaphoreKHR(sema_2);
    test_error(err, "Could not release semaphore");

    return TEST_PASS;
}

// Confirm that we can wait for multiple semaphores with one command
int test_semaphores_multi_wait(cl_device_id deviceID, cl_context context,
                               cl_command_queue defaultQueue, int num_elements)
{
    cl_int err;

    if (!is_extension_available(deviceID, "cl_khr_semaphore"))
    {
        log_info("cl_khr_semaphore is not supported on this platform. "
                 "Skipping test.\n");
        return TEST_SKIPPED_ITSELF;
    }

    // Obtain pointers to semaphore's API
    GET_PFN(deviceID, clCreateSemaphoreWithPropertiesKHR);
    GET_PFN(deviceID, clEnqueueSignalSemaphoresKHR);
    GET_PFN(deviceID, clEnqueueWaitSemaphoresKHR);
    GET_PFN(deviceID, clReleaseSemaphoreKHR);

    // Create ooo queue
    clCommandQueueWrapper queue = clCreateCommandQueue(
        context, deviceID, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &err);
    test_error(err, "Could not create command queue");

    // Create semaphores
    cl_semaphore_properties_khr sema_props[] = {
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_KHR),
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_BINARY_KHR),
        0
    };
    cl_semaphore_khr sema_1 =
        clCreateSemaphoreWithPropertiesKHR(context, sema_props, &err);
    test_error(err, "Could not create semaphore");

    cl_semaphore_khr sema_2 =
        clCreateSemaphoreWithPropertiesKHR(context, sema_props, &err);
    test_error(err, "Could not create semaphore");

    // Signal semaphore 1
    clEventWrapper signal_1_event;
    err = clEnqueueSignalSemaphoresKHR(queue, 1, &sema_1, nullptr, 0, nullptr,
                                       &signal_1_event);
    test_error(err, "Could not signal semaphore");

    // Signal semaphore 2
    clEventWrapper signal_2_event;
    err = clEnqueueSignalSemaphoresKHR(queue, 1, &sema_2, nullptr, 0, nullptr,
                                       &signal_2_event);
    test_error(err, "Could not signal semaphore");

    // Wait semaphore 1 and 2
    clEventWrapper wait_event;
    cl_semaphore_khr sema_list[] = { sema_1, sema_2 };
    err = clEnqueueWaitSemaphoresKHR(queue, 2, sema_list, nullptr, 0, nullptr,
                                     &wait_event);
    test_error(err, "Could not wait semaphore");

    // Finish
    err = clFinish(queue);
    test_error(err, "Could not finish queue");

    // Ensure all events are completed
    test_assert_event_complete(signal_1_event);
    test_assert_event_complete(signal_2_event);
    test_assert_event_complete(wait_event);

    // Release semaphores
    err = clReleaseSemaphoreKHR(sema_1);
    test_error(err, "Could not release semaphore");

    err = clReleaseSemaphoreKHR(sema_2);
    test_error(err, "Could not release semaphore");

    return TEST_PASS;
}

// Confirm the semaphores can be successfully queried
int test_semaphores_queries(cl_device_id deviceID, cl_context context,
                            cl_command_queue defaultQueue, int num_elements)
{
    cl_int err = CL_SUCCESS;

    if (!is_extension_available(deviceID, "cl_khr_semaphore"))
    {
        log_info("cl_khr_semaphore is not supported on this platform. "
                 "Skipping test.\n");
        return TEST_SKIPPED_ITSELF;
    }

    // Obtain pointers to semaphore's API
    GET_PFN(deviceID, clCreateSemaphoreWithPropertiesKHR);
    GET_PFN(deviceID, clGetSemaphoreInfoKHR);
    GET_PFN(deviceID, clRetainSemaphoreKHR);
    GET_PFN(deviceID, clReleaseSemaphoreKHR);

    // Create binary semaphore
    cl_semaphore_properties_khr sema_props[] = {
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_KHR),
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_BINARY_KHR),
        static_cast<cl_semaphore_properties_khr>(
            CL_SEMAPHORE_DEVICE_HANDLE_LIST_KHR),
        (cl_semaphore_properties_khr)deviceID,
        CL_SEMAPHORE_DEVICE_HANDLE_LIST_END_KHR,
        0
    };
    cl_semaphore_khr sema =
        clCreateSemaphoreWithPropertiesKHR(context, sema_props, &err);
    test_error(err, "Could not create semaphore");

    // Confirm that querying CL_SEMAPHORE_TYPE_KHR returns
    // CL_SEMAPHORE_TYPE_BINARY_KHR
    SEMAPHORE_PARAM_TEST(CL_SEMAPHORE_TYPE_KHR, cl_semaphore_type_khr,
                         CL_SEMAPHORE_TYPE_BINARY_KHR);

    // Confirm that querying CL_SEMAPHORE_CONTEXT_KHR returns the right context
    SEMAPHORE_PARAM_TEST(CL_SEMAPHORE_CONTEXT_KHR, cl_context, context);

    // Confirm that querying CL_SEMAPHORE_REFERENCE_COUNT_KHR returns the right
    // value
    SEMAPHORE_PARAM_TEST(CL_SEMAPHORE_REFERENCE_COUNT_KHR, cl_uint, 1);

    // Confirm that querying CL_SEMAPHORE_DEVICE_HANDLE_LIST_KHR returns the
    // same device id the semaphore was created with
    SEMAPHORE_PARAM_TEST(CL_SEMAPHORE_DEVICE_HANDLE_LIST_KHR, cl_device_id,
                         deviceID);

    err = clRetainSemaphoreKHR(sema);
    test_error(err, "Could not retain semaphore");
    SEMAPHORE_PARAM_TEST(CL_SEMAPHORE_REFERENCE_COUNT_KHR, cl_uint, 2);

    err = clReleaseSemaphoreKHR(sema);
    test_error(err, "Could not release semaphore");
    SEMAPHORE_PARAM_TEST(CL_SEMAPHORE_REFERENCE_COUNT_KHR, cl_uint, 1);

    // Confirm that querying CL_SEMAPHORE_PROPERTIES_KHR returns the same
    // properties the semaphore was created with
    SEMAPHORE_PARAM_TEST_ARRAY(CL_SEMAPHORE_PROPERTIES_KHR,
                               cl_semaphore_properties_khr, 6, sema_props);

    // Confirm that querying CL_SEMAPHORE_PAYLOAD_KHR returns the unsignaled
    // state
    SEMAPHORE_PARAM_TEST(CL_SEMAPHORE_PAYLOAD_KHR, cl_semaphore_payload_khr, 0);

    err = clReleaseSemaphoreKHR(sema);
    test_error(err, "Could not release semaphore");

    return TEST_PASS;
}

// Test it is possible to export a semaphore to a sync fd and import the same
// sync fd to a new semaphore
int test_semaphores_import_export_fd(cl_device_id deviceID, cl_context context,
                                     cl_command_queue defaultQueue,
                                     int num_elements)
{
    cl_int err;

    if (!is_extension_available(deviceID, "cl_khr_semaphore"))
    {
        log_info("cl_khr_semaphore is not supported on this platform. "
                 "Skipping test.\n");
        return TEST_SKIPPED_ITSELF;
    }

    if (!is_extension_available(deviceID, "cl_khr_external_semaphore_sync_fd"))
    {
        log_info("cl_khr_external_semaphore_sync_fd is not supported on this "
                 "platform. Skipping test.\n");
        return TEST_SKIPPED_ITSELF;
    }

    // Obtain pointers to semaphore's API
    GET_PFN(deviceID, clCreateSemaphoreWithPropertiesKHR);
    GET_PFN(deviceID, clEnqueueSignalSemaphoresKHR);
    GET_PFN(deviceID, clEnqueueWaitSemaphoresKHR);
    GET_PFN(deviceID, clGetSemaphoreHandleForTypeKHR);
    GET_PFN(deviceID, clReleaseSemaphoreKHR);

    // Create ooo queue
    clCommandQueueWrapper queue = clCreateCommandQueue(
        context, deviceID, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &err);
    test_error(err, "Could not create command queue");

    // Create semaphore
    cl_semaphore_properties_khr sema_1_props[] = {
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_KHR),
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_BINARY_KHR),
        static_cast<cl_semaphore_properties_khr>(
            CL_SEMAPHORE_EXPORT_HANDLE_TYPES_KHR),
        static_cast<cl_semaphore_properties_khr>(
            CL_SEMAPHORE_HANDLE_SYNC_FD_KHR),
        static_cast<cl_semaphore_properties_khr>(
            CL_SEMAPHORE_EXPORT_HANDLE_TYPES_LIST_END_KHR),
        0
    };
    cl_semaphore_khr sema_1 =
        clCreateSemaphoreWithPropertiesKHR(context, sema_1_props, &err);
    test_error(err, "Could not create semaphore");

    // Signal semaphore
    clEventWrapper signal_event;
    err = clEnqueueSignalSemaphoresKHR(queue, 1, &sema_1, nullptr, 0, nullptr,
                                       &signal_event);
    test_error(err, "Could not signal semaphore");

    // Extract sync fd
    int handle = -1;
    size_t handle_size;
    err = clGetSemaphoreHandleForTypeKHR(sema_1, deviceID,
                                         CL_SEMAPHORE_HANDLE_SYNC_FD_KHR,
                                         sizeof(handle), &handle, &handle_size);
    test_error(err, "Could not extract semaphore handle");
    test_assert_error(sizeof(handle) == handle_size, "Invalid handle size");
    test_assert_error(handle >= 0, "Invalid handle");

    // Create semaphore from sync fd
    cl_semaphore_properties_khr sema_2_props[] = {
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_KHR),
        static_cast<cl_semaphore_properties_khr>(CL_SEMAPHORE_TYPE_BINARY_KHR),
        CL_SEMAPHORE_HANDLE_SYNC_FD_KHR,
        static_cast<cl_semaphore_properties_khr>(handle), 0
    };

    cl_semaphore_khr sema_2 =
        clCreateSemaphoreWithPropertiesKHR(context, sema_2_props, &err);
    test_error(err, "Could not create semaphore");

    // Wait semaphore
    clEventWrapper wait_event;
    err = clEnqueueWaitSemaphoresKHR(queue, 1, &sema_2, nullptr, 0, nullptr,
                                     &wait_event);
    test_error(err, "Could not wait semaphore");

    // Finish
    err = clFinish(queue);
    test_error(err, "Could not finish queue");

    // Check all events are completed
    test_assert_event_complete(signal_event);
    test_assert_event_complete(wait_event);

    // Release semaphore
    err = clReleaseSemaphoreKHR(sema_1);
    test_error(err, "Could not release semaphore");

    err = clReleaseSemaphoreKHR(sema_2);
    test_error(err, "Could not release semaphore");
    return TEST_PASS;
}
