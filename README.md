# About

This is a grayscale image compression project based on the general JPEG-style approach of compression.<br>
It uses the 2D Discrete Cosine Transform (DCT) as the core operation in a pipeline that reduces image size while attempting to retain acceptable visual quality.<br>
The system was developed using the CrossCore Embedded Studio (CCES) development environment and is meant to be used on the ADSP-21489 platform, with image upload to the platform mechanism and supporting result analysis done in Python.<br> Additionally, an RGB variation of the project is also included. 

### **Python:**  
generateHeader.py - Generates a header file containing image dimensions and rgb pixels that we can then upload to our DSP.
decompress_to_bmp.py - Decompresses the generated compressed file by applying the corresponding inverse processing steps and outputs a BMP image
