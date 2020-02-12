
#include <k4a/k4a.h>

#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/vector.h>
#include <grass/glocale.h>


void *get_cell_ptr(void *array, int cols, int row, int col,
                   RASTER_MAP_TYPE map_type)
{
    return G_incr_void_ptr(array,
                           ((row * (size_t) cols) +
                            col) * Rast_cell_size(map_type));
}

void binning(k4a_float3_t *cloud, unsigned npoints,
             char* output, struct bound_box *bbox, double resolution,
             double scale, double zexag, double bottom, double offset,
             const char *method_name) {

    struct Cell_head cellhd;

    G_get_set_window(&cellhd);
    cellhd.north = bbox->N;
    cellhd.south = bbox->S;
    cellhd.west = bbox->W;
    cellhd.east = bbox->E;
    cellhd.ns_res = resolution;
    cellhd.ew_res = resolution;
    G_adjust_Cell_head(&cellhd, 0, 0);
    Rast_set_window(&cellhd);

    int method = 0; // mean
    if (strcmp(method_name, "min") == 0)
        method = 1; // min
    else if (strcmp(method_name, "max") == 0)
        method = 2; // max

    /* open output map */
    int out_fd = Rast_open_new(output, FCELL_TYPE);

    /* allocate memory for a single row of output data */
    void *raster_row = Rast_allocate_output_buf(FCELL_TYPE);

    void *n_array = G_calloc((size_t) cellhd.rows * (cellhd.cols + 1),
                             Rast_cell_size(CELL_TYPE));
    void *sum_array = G_calloc((size_t) cellhd.rows * (cellhd.cols + 1),
                               Rast_cell_size(FCELL_TYPE));

    int arr_row, arr_col;
    double z;
    for (int i = 0; i < npoints; i++) {
        /* find the bin in the current array box */
        arr_row = (int)((cellhd.north - cloud[i].xyz.y) / cellhd.ns_res);
        arr_col = (int)((cloud[i].xyz.x - cellhd.west) / cellhd.ew_res);

        if (arr_row < 0 || arr_row >= cellhd.rows || arr_col < 0 || arr_col >= cellhd.cols){
            continue;
        }
        z = (cloud[i].xyz.z - bottom) * scale / zexag + offset;

        void *ptr_n = get_cell_ptr(n_array, cellhd.cols, arr_row, arr_col, CELL_TYPE);
        CELL old_n = Rast_get_c_value(ptr_n, CELL_TYPE);
        Rast_set_c_value(ptr_n, (1 + old_n), CELL_TYPE);

        void *ptr_sum = get_cell_ptr(sum_array, cellhd.cols, arr_row, arr_col, FCELL_TYPE);
        FCELL old_sum = Rast_get_f_value(ptr_sum, FCELL_TYPE);
        if (method == 0 || old_n == 0)
            Rast_set_f_value(ptr_sum, (z + old_sum), FCELL_TYPE);
        else if (method == 1)
            Rast_set_f_value(ptr_sum, z < old_sum ? z : old_sum, FCELL_TYPE);
        else
            Rast_set_f_value(ptr_sum, z > old_sum ? z : old_sum, FCELL_TYPE);
    }

    /* calc stats and output */
    G_message(_("Writing to map ..."));
    for (int row = 0; row < cellhd.rows; row++) {
        void *ptr = raster_row;
        for (int col = 0; col < cellhd.cols; col++) {
            size_t offset = (row * cellhd.cols + col) * Rast_cell_size(FCELL_TYPE);
            size_t n_offset = (row * cellhd.cols + col) * Rast_cell_size(CELL_TYPE);
            int n = Rast_get_c_value(G_incr_void_ptr(n_array, n_offset), CELL_TYPE);
            double sum =
                Rast_get_d_value(G_incr_void_ptr(sum_array, offset), FCELL_TYPE);

            if (n == 0) {
                int count = 0;
                double sum2 = 0;
                int window_size = 1;
                int nn;
                for (int rr = row - window_size; rr <= row + window_size; rr++) {
                    for (int cc = col - window_size; cc <= col + window_size; cc++) {
                        if (cc < 0 || rr < 0 || cc >= cellhd.cols || rr >= cellhd.rows)
                            continue;
                        void *ptr2 = get_cell_ptr(n_array, cellhd.cols,
                                            rr, cc, CELL_TYPE);
                        void *ptr3 = get_cell_ptr(sum_array, cellhd.cols,
                                            rr, cc, FCELL_TYPE);
                        if ((nn = Rast_get_c_value(ptr2, CELL_TYPE))) {
                            if (method == 0)
                                sum2 += (Rast_get_f_value(ptr3, FCELL_TYPE) / nn);
                            else
                                sum2 += (Rast_get_f_value(ptr3, FCELL_TYPE));
                            count += 1;
                        }
                    }
                }
                if (count >= 3) {
                    Rast_set_f_value(ptr, sum2 / count, FCELL_TYPE);
                }
                else
                    Rast_set_null_value(ptr, 1, FCELL_TYPE);
            }
            else {
                if (method == 0)
                    Rast_set_d_value(ptr, (sum / n), FCELL_TYPE);
                else
                    Rast_set_d_value(ptr, sum, FCELL_TYPE);
            }

            ptr = G_incr_void_ptr(ptr, Rast_cell_size(FCELL_TYPE));
        }
        /* write out line of raster data */
        Rast_put_row(out_fd, raster_row, FCELL_TYPE);
    }

    /* free memory */
    G_free(n_array);
    G_free(sum_array);
    G_free(raster_row);
    /* close raster file & write history */
    Rast_close(out_fd);
}