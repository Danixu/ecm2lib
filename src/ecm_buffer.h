#include "ecm_enums.h"

#ifndef __ECM_BUFFER_H__
#define __ECM_BUFFER_H__
namespace ecm
{
    template <typename T>
    struct data_buffer
    {
        std::vector<T> buffer;
        size_t current_position = 0;
        size_t start_position = 0;

        data_buffer(size_t buffer_size = 0)
        {
            if (buffer_size > 0)
            {
                buffer.resize(buffer_size);
            }
        }

        T *get_current_data_position()
        {
            /* The current position is higher than the size of the buffer */
            if (current_position > buffer.size())
            {
                return nullptr;
            }

            return (T *)(buffer.data() + (current_position * sizeof(T)));
        }

        T *get_start_data_position()
        {
            /* The current position is higher than the size of the buffer */
            if (start_position > buffer.size())
            {
                return nullptr;
            }

            return (T *)(buffer.data() + (start_position * sizeof(T)));
        }

        void update_start_position()
        {
            start_position = current_position;
        }

        void reset_positions()
        {
            start_position = 0;
            current_position = 0;
        }

        void revert_current_position()
        {
            current_position = start_position;
        }

        status_code write(std::vector<T> data, size_t stop_after = 0)
        {
            if (stop_after == 0)
            {
                stop_after = data.size();
            }

            if ((current_position + stop_after) > buffer.size())
            {
                return STATUS_ERROR_NO_ENOUGH_OUTPUT_BUFFER_SPACE;
            }

            std::memcpy(get_current_data_position(), data.data(), stop_after);
            return STATUS_OK;
        }

        /**
         * @brief Get the items left in the buffer
         *
         * @return size_t The buffer items available in the buffer
         */
        size_t get_available_items()
        {
            if (current_position > buffer.size())
            {
                return 0;
            }
            else
            {
                return buffer.size() - current_position;
            }
        }

        /**
         * @brief Moves the data in the buffer vector from the source position to the destination one
         *
         * @param source (uint64_t) Source position of the data to be moved
         * @param destination (uint64_t) Destination position where the data will be moved
         * @param bytes_to_move (uint64_t) How much data to move
         * @param resize_buffer If true, the buffer will be resize if required on data move
         * @return true The data was moved sucessfully
         * @return false There was an error moving the data
         */
        inline int move_data(
            uint64_t source,
            uint64_t destination,
            uint64_t elements_to_move,
            bool resize_buffer = false)
        {
            /* Check if source and destination are equals */
            if (source == destination)
            {
                /* Really? */
                return -1;
            }
            /* Check the source position */
            if ((source + elements_to_move) > buffer.size())
            {
                /* The source data is out of bounds */
                return -2;
            }
            /* Check if a resize is required */
            if ((destination + elements_to_move) > buffer.size())
            {
                if (resize_buffer == false)
                {
                    // The buffer doesn't have enough space to move the data and the resize option is false
                    return -3;
                }
                else
                {
                    buffer.resize(destination + elements_to_move);
                }
            }

            /* Move the data checking the best way */
            if ((source + elements_to_move) >= destination)
            {
                /* The destination overlap the source from the end, so we will start moving from the end. */
                std::move_backward(buffer.data() + (source * sizeof(T)), buffer.data() + ((source + elements_to_move) * sizeof(T)), buffer.data() + (destination * sizeof(T)));
            }
            else
            {
                /* The source and destination doesn't overlap, or the overlapping happens from the start */
                std::move(buffer.data() + (source * sizeof(T)), buffer.data() + ((source + elements_to_move) * sizeof(T)), buffer.data() + (destination * sizeof(T)));
            }

            return 0;
        }

        char operator[](size_t item)
        {
            return get_current_data_position()[item];
        }

        char *operator+(size_t sum)
        {
            return (get_current_data_position() + sum);
        }
    };
}
#endif