package dev.aifo.fastslide.tools;

import dev.aifo.fastslide.ChannelMetadata;
import dev.aifo.fastslide.FastSlide;
import dev.aifo.fastslide.Image;
import dev.aifo.fastslide.ImageFormat;
import dev.aifo.fastslide.ImageInfo;
import dev.aifo.fastslide.SlideImage;
import dev.aifo.fastslide.SlideReader;
import java.util.List;

/** Temporary diagnostic mirroring the QuPath FastSlide server's metadata path. */
public final class ChannelDiag {
  private ChannelDiag() {}

  public static void main(String[] args) throws Exception {
    String input = args[0];
    try (SlideReader reader = FastSlide.open(input)) {
      int imageCount = reader.getImageCount();
      int primary = reader.getPrimaryImageIndex();
      System.out.println("imageCount=" + imageCount + " primary=" + primary);

      try (SlideImage image = reader.getImage(primary)) {
        ImageFormat format = image.getImageFormat();
        System.out.println("image.getImageFormat()=" + format);
        System.out.println("image.getDataType()=" + image.getDataType());

        List<ChannelMetadata> chans = image.getChannelMetadata();
        System.out.println("image.getChannelMetadata().size()=" + chans.size());
        for (ChannelMetadata c : chans) {
          System.out.printf(
              "  name=%-8s biomarker=%-18s rgb=(%d,%d,%d) exp=%d units=%d%n",
              c.name(), c.biomarker(), c.colorR(), c.colorG(), c.colorB(),
              c.exposureTime(), c.signalUnits());
        }

        int level = 0;
        int w = 256;
        int h = 256;
        try (Image tile = image.readRegion(0, 0, w, h, level, 0, 0)) {
          ImageInfo info = tile.getInfo();
          System.out.println("tile.getInfo()=" + info);
          byte[] data = tile.copyData();
          System.out.println("copyData().length=" + data.length);
          long expected =
              (long) info.width() * info.height() * info.channels() * info.bytesPerSample();
          System.out.println("expected bytes (w*h*ch*bps)=" + expected);
        }
      }
    }
  }
}
