"""SimpleTIFF: High-performance TIFF reader with Python bindings.

Example usage:
    >>> import simpletiff
    >>> with simpletiff.SimpleTiffReader.from_file_path("image.tiff") as reader:
    ...     print(f"Pages: {reader.num_pages}")
    ...     page = reader[0]
    ...     data = page.read()
    ...     print(f"Shape: {data.shape}")
"""

from enum import IntEnum

from simpletiff._simpletiff import SimpleTiffPage, SimpleTiffReader


class Compression(IntEnum):
    """TIFF compression types.

    This enum maps TIFF compression codes to human-readable names.
    You can compare with integers or use the name attribute.

    Example:
        >>> page.compression == Compression.JPEG
        True
        >>> page.compression.name
        'JPEG'
        >>> int(page.compression)
        7
    """

    NONE = 1
    """Uncompressed"""

    LZW = 5
    """LZW compression"""

    JPEG = 7
    """JPEG compression"""

    DEFLATE = 8
    """Deflate/ZIP compression"""

    ZSTD = 50000
    """ZSTD compression (vendor-specific code)"""

    UNKNOWN = 0
    """Unknown or unsupported compression"""

    @classmethod
    def from_code(cls, code: int) -> "Compression":
        """Convert a compression code to a Compression enum.

        Args:
            code: TIFF compression code

        Returns:
            Compression enum value, or UNKNOWN if not recognized
        """
        try:
            return cls(code)
        except ValueError:
            return cls.UNKNOWN


class Photometric(IntEnum):
    """TIFF photometric interpretation types.

    Describes the color space of the image data.

    Example:
        >>> page.photometric_interpretation == Photometric.RGB
        True
        >>> page.photometric_interpretation.name
        'RGB'
    """

    MIN_IS_WHITE = 0
    """Minimum value is white (grayscale)"""

    MIN_IS_BLACK = 1
    """Minimum value is black (grayscale)"""

    RGB = 2
    """RGB color space"""

    PALETTE = 3
    """Palette/indexed color"""

    TRANSPARENCY_MASK = 4
    """Transparency mask"""

    CMYK = 5
    """CMYK color space"""

    YCBCR = 6
    """YCbCr color space (often used with JPEG)"""

    CIELAB = 8
    """CIE L*a*b* color space"""

    ICCLAB = 9
    """ICC L*a*b* color space"""

    ITULAB = 10
    """ITU L*a*b* color space"""

    UNKNOWN = 0xFFFF
    """Unknown or unsupported photometric interpretation"""

    @classmethod
    def from_code(cls, code: int) -> "Photometric":
        """Convert a photometric code to a Photometric enum.

        Args:
            code: TIFF photometric interpretation code

        Returns:
            Photometric enum value, or UNKNOWN if not recognized
        """
        try:
            return cls(code)
        except ValueError:
            return cls.UNKNOWN


__all__ = ["SimpleTiffReader", "SimpleTiffPage", "Compression", "Photometric"]
__version__ = "0.1.0"
