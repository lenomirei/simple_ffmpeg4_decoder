#include <stdio.h>
#include <stdlib.h>

#define __STDC_CONSTANT_MACROS

#include <libavcodec/avcodec.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>



int refcount = 0;

int OpenCodecContext(int *stream_idx,
	AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type, char* src_filename)
{
	int ret, stream_index;
	AVStream *st;
	AVCodec *dec = NULL;

	ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
	if (ret < 0) {
		fprintf(stderr, "Could not find %s stream in input file '%s'\n",
			av_get_media_type_string(type), src_filename);
		return ret;
	}
	else {
		stream_index = ret;
		st = fmt_ctx->streams[stream_index];

		/* find decoder for the stream */
		dec = avcodec_find_decoder(st->codecpar->codec_id);
		if (!dec) {
			fprintf(stderr, "Failed to find %s codec\n",
				av_get_media_type_string(type));
			return AVERROR(EINVAL);
		}

		/* Allocate a codec context for the decoder */
		*dec_ctx = avcodec_alloc_context3(dec);
		if (!*dec_ctx) {
			fprintf(stderr, "Failed to allocate the %s codec context\n",
				av_get_media_type_string(type));
			return AVERROR(ENOMEM);
		}

		/* Copy codec parameters from input stream to output codec context */
		if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
			fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
				av_get_media_type_string(type));
			return ret;
		}

		/* Init the decoders, with or without reference counting */
		if ((ret = avcodec_open2(*dec_ctx, dec, NULL)) < 0) {
			fprintf(stderr, "Failed to open %s codec\n",
				av_get_media_type_string(type));
			return ret;
		}
		*stream_idx = stream_index;
	}

	return 0;
}


int main() 
{
	AVFormatContext	*format_context = NULL;
	static int video_stream_idx = -1;
	AVCodecContext	*codec_context = NULL;
	AVCodec			*codec = NULL;
	AVFrame	*frame, *frame_YUV = NULL;
	unsigned char *out_buffer = NULL;
	AVPacket packet;
	AVStream *video_stream = NULL;
	enum AVPixelFormat pix_fmt;
	int y_size;
	int width, height;
	int ret;
	struct SwsContext *img_convert_ctx = NULL;

	char filepath[] = "¡¾MV¡¿Lucky¡îOrb feat. Hatsune Miku by emon(Tes.)  - ¥é¥Ã¥­©`¡î¥ª©`¥Ö feat. ³õÒô¥ß¥¯ by emon(Tes.) ¡¾MIKU EXPO 5th¡¿.webm";
	//char filepath[] = "udp://@224.4.5.6:1234";
	char output_filepath[] = "output.yuv";
	FILE *output_file = NULL;

	if (avformat_open_input(&format_context, filepath, NULL, NULL) < 0) {
		fprintf(stderr, "Could not open source file %s\n", filepath);
		exit(1);
	}

	if (avformat_find_stream_info(format_context, NULL) < 0) {
		fprintf(stderr, "Could not find stream information\n");
		exit(1);
	}

	if (OpenCodecContext(&video_stream_idx, &codec_context, format_context, AVMEDIA_TYPE_VIDEO, filepath) >= 0) {
		video_stream = format_context->streams[video_stream_idx];
		errno_t err;
		err = fopen_s(&output_file, output_filepath, "wb+");
		if (!output_file) {
			fprintf(stderr, "Could not open destination file %s\n", output_filepath);
			return -1;
		}

		/* allocate image where the decoded image will be put */
		width = codec_context->width;
		height = codec_context->height;
		pix_fmt = codec_context->pix_fmt;
	}
	if (!video_stream) {
		fprintf(stderr, "Could not find audio or video stream in the input, aborting\n");
		return -1;
	}

	frame = av_frame_alloc();
	frame_YUV = av_frame_alloc();
	out_buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, codec_context->width, codec_context->height, 1));
	av_image_fill_arrays(frame_YUV->data, frame_YUV->linesize, out_buffer,
		AV_PIX_FMT_YUV420P, codec_context->width, codec_context->height, 1);
	if (!frame && !frame_YUV) {
		fprintf(stderr, "Could not allocate frame\n");
		return AVERROR(ENOMEM);
	}

	av_init_packet(&packet);
	packet.data = NULL;
	packet.size = 0;

	
	

	img_convert_ctx = sws_getContext(codec_context->width, codec_context->height, codec_context->pix_fmt,
		codec_context->width, codec_context->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	// read frames from the input file
	while (av_read_frame(format_context, &packet) >= 0) {
		if (packet.stream_index == video_stream_idx) {
			ret = avcodec_send_packet(codec_context, &packet);
			if (ret < 0) {
				fprintf(stderr, "Error sending a packet for decoding\n");
			}
			while (ret >= 0) {
				ret = avcodec_receive_frame(codec_context, frame);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
					break;
				else if (ret < 0) {
					printf("Decode Error.\n");
				}


				/* the picture is allocated by the decoder. no need to
				   free it */
				sws_scale(img_convert_ctx, (const unsigned char* const*)frame->data, frame->linesize, 0, codec_context->height,
					frame_YUV->data, frame_YUV->linesize);
				y_size = codec_context->width*codec_context->height;
				fwrite(frame_YUV->data[0], 1, y_size, output_file);    //Y 
				fwrite(frame_YUV->data[1], 1, y_size / 4, output_file);  //U
				fwrite(frame_YUV->data[2], 1, y_size / 4, output_file);  //V
				printf("Succeed to decode 1 frame!\n");
			}
		}
	}

	sws_freeContext(img_convert_ctx);

	fclose(output_file);

	av_frame_free(&frame_YUV);
	av_frame_free(&frame);
	avcodec_free_context(&codec_context);
	avformat_close_input(&format_context);

	return 0;
}